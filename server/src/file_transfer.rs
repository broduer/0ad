use std::collections::HashMap;

use rusty_enet::PeerID;

use crate::{
    messages::{
        FileTransferAck, FileTransferRequest, FileTransferResponse, FiletransferData, NetMessage,
    },
    network_message::OutboundNetworkMessage,
};

const DEFAULT_PACKET_SIZE: usize = 1024;
const MAX_FILE_TRANSFER_SIZE: usize = 8 * 1024 * 1024; // 8 MiB

/// Request type codes matching C++ CNetFileTransferer::RequestType.
pub const REQUEST_TYPE_REJOIN: i8 = 1;

struct ReceiveTask {
    /// The rejoining client's host_id - used to look them up when transfer completes.
    pub target_host_id: u32,
    /// Total expected length, set once we receive the FileTransferResponse.
    pub length: Option<u32>,
    /// Accumulates incoming data.
    pub buffer: Vec<u8>,
}

/// Handles both sides of the server-side file transfer relay:
/// - **Receive side**: request game state FROM an existing ingame client.
/// - **Send side**: serve game state TO a rejoining client.
pub struct FileTransferManager {
    next_request_id: u32,
    /// Pending receive operations (request_id -> task).
    receive_tasks: HashMap<u32, ReceiveTask>,
}

impl FileTransferManager {
    pub fn new() -> Self {
        Self {
            next_request_id: 0,
            receive_tasks: HashMap::new(),
        }
    }

    /// Returns true if the message type is one we handle here.
    pub fn is_file_transfer_message(msg: &NetMessage) -> bool {
        matches!(
            msg,
            NetMessage::FileTransferRequest(_)
                | NetMessage::FileTransferResponse(_)
                | NetMessage::FiletransferData(_)
                | NetMessage::FileTransferAck(_)
        )
    }

    // -------------------------------------------------------------------------
    // Receive side (server <- source client)
    // -------------------------------------------------------------------------

    /// Start a file transfer request to `source_peer`.  Sends a
    /// `FileTransferRequest` and records the pending task so we can match the
    /// response to the `target_host_id`.
    ///
    /// Returns the serialized `FileTransferRequest` message to be sent to the
    /// source peer and the outbound message to actually deliver it.
    pub fn start_request(
        &mut self,
        source_peer: PeerID,
        target_host_id: u32,
    ) -> OutboundNetworkMessage {
        let request_id = self.next_request_id;
        self.next_request_id += 1;

        self.receive_tasks.insert(
            request_id,
            ReceiveTask {
                target_host_id,
                length: None,
                buffer: Vec::new(),
            },
        );

        let msg = NetMessage::FileTransferRequest(FileTransferRequest {
            request_type: REQUEST_TYPE_REJOIN,
            request_id,
        });

        OutboundNetworkMessage::Message {
            peer: source_peer,
            data: msg.to_bytes(),
        }
    }

    /// Process an incoming `FileTransferResponse` (from the source client).
    /// Returns `Err` if this response has no matching pending task or if the
    /// length is invalid; in those cases the caller should log and ignore.
    pub fn handle_response(&mut self, msg: &FileTransferResponse) -> Result<(), String> {
        let task = self
            .receive_tasks
            .get_mut(&msg.request_id)
            .ok_or_else(|| format!("unsolicited FileTransferResponse id={}", msg.request_id))?;

        if msg.length == 0 || msg.length > MAX_FILE_TRANSFER_SIZE as u32 {
            return Err(format!(
                "invalid FileTransferResponse length={}",
                msg.length
            ));
        }

        task.length = Some(msg.length);
        task.buffer.reserve(msg.length as usize);
        Ok(())
    }

    /// Process an incoming `FileTransferData` packet (from the source client).
    ///
    /// Returns the ACK message to send back to the source peer, and optionally
    /// the completed buffer + `target_host_id` when the transfer is done.
    pub fn handle_data(
        &mut self,
        source_peer: PeerID,
        msg: &FiletransferData,
    ) -> Result<(OutboundNetworkMessage, Option<(u32, Vec<u8>)>), String> {
        let task = self
            .receive_tasks
            .get_mut(&msg.request_id)
            .ok_or_else(|| format!("unsolicited FileTransferData id={}", msg.request_id))?;

        let expected = task
            .length
            .ok_or_else(|| format!("FileTransferData before response id={}", msg.request_id))?;

        task.buffer.extend_from_slice(&msg.data);

        if task.buffer.len() > expected as usize {
            return Err(format!(
                "FileTransferData overflow: expected={} got={}",
                expected,
                task.buffer.len()
            ));
        }

        let ack = OutboundNetworkMessage::Message {
            peer: source_peer,
            data: NetMessage::FileTransferAck(FileTransferAck {
                request_id: msg.request_id,
                num_packets: 1,
            })
            .to_bytes(),
        };

        if task.buffer.len() == expected as usize {
            // Transfer complete -- extract task
            let task = self.receive_tasks.remove(&msg.request_id).unwrap();
            return Ok((ack, Some((task.target_host_id, task.buffer))));
        }

        Ok((ack, None))
    }

    // -------------------------------------------------------------------------
    // Send side (server -> rejoining client)
    // -------------------------------------------------------------------------

    /// Serve a `FileTransferRequest` from the rejoining client by sending the
    /// entire `data` buffer immediately (simplified: no windowing, ENet provides
    /// reliability).  Returns the sequence of messages to send to `peer`.
    pub fn handle_request(
        &self,
        peer: PeerID,
        msg: &FileTransferRequest,
        data: &[u8],
    ) -> Vec<OutboundNetworkMessage> {
        let mut out = Vec::new();

        out.push(OutboundNetworkMessage::Message {
            peer,
            data: NetMessage::FileTransferResponse(FileTransferResponse {
                request_id: msg.request_id,
                length: data.len() as u32,
            })
            .to_bytes(),
        });

        for chunk in data.chunks(DEFAULT_PACKET_SIZE) {
            out.push(OutboundNetworkMessage::Message {
                peer,
                data: NetMessage::FiletransferData(FiletransferData {
                    request_id: msg.request_id,
                    data: chunk.to_vec(),
                })
                .to_bytes(),
            });
        }

        out
    }
}

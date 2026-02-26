use std::net::IpAddr;

use rusty_enet::PeerID;

#[derive(Debug)]
pub enum InboundNetworkMessage {
    Connect {
        ip: IpAddr,
        peer: PeerID,
    },
    Disconnect {
        ip: IpAddr,
        peer: PeerID,
        reason: u32,
    },
    Message {
        ip: IpAddr,
        peer: PeerID,
        data: Vec<u8>,
    },
}

#[derive(Debug)]
pub enum OutboundNetworkMessage {
    Message { peer: PeerID, data: Vec<u8> },
    Disconnect { peer: PeerID, reason: u32 },
}

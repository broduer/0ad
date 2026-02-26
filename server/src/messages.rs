use crate::utils::hex_dump;

mod assign_player;
mod authenticate;
mod authenticate_result;
mod authenticate_result_code;
mod chat;
mod client_handshake;
mod client_paused;
mod client_performance;
mod client_timeout;
mod clients_loading;
mod enabled_mod;
mod end_command_batch;
mod file_transfer_ack;
mod file_transfer_data;
mod file_transfer_request;
mod file_transfer_response;
mod flare;
mod game_saved_start;
mod game_setup;
mod game_start;
mod guid;
mod host;
mod join_sync_start;
mod kicked;
mod loaded_game;
mod performance_entry;
mod player_assignment;
mod ready;
mod rejoined;
mod server_handshake;
mod server_handshake_response;
mod simulation;
mod sync_check;
mod sync_error;

pub use assign_player::AssignPlayer;
pub use authenticate::Authenticate;
pub use authenticate_result::AuthenticateResult;
pub use authenticate_result_code::AuthenticateResultCode;
pub use chat::Chat;
pub use client_handshake::ClientHandshake;
pub use client_paused::ClientPaused;
pub use client_performance::ClientPerformance;
pub use client_timeout::ClientTimeout;
pub use clients_loading::ClientsLoading;
pub use enabled_mod::EnabledMod;
pub use end_command_batch::EndCommandBatch;
pub use file_transfer_ack::FileTransferAck;
pub use file_transfer_data::FiletransferData;
pub use file_transfer_request::FileTransferRequest;
pub use file_transfer_response::FileTransferResponse;
pub use flare::Flare;
pub use game_saved_start::GameSavedStart;
pub use game_setup::GameSetup;
pub use game_start::GameStart;
pub use guid::Guid;
pub use host::Host;
pub use join_sync_start::JoinSyncStart;
pub use kicked::Kicked;
pub use loaded_game::LoadedGame;
pub use player_assignment::PlayerAssignment;
pub use ready::Ready;
pub use rejoined::Rejoined;
pub use server_handshake::ServerHandshake;
pub use server_handshake_response::ServerHandshakeResponse;
pub use simulation::Simulation;
pub use sync_check::SyncCheck;
pub use sync_error::SyncError;

#[derive(Debug)]
pub(crate) enum NetMessage {
    ConnectComplete,
    ConnectionLost,
    Invalid,

    ServerHandshake(ServerHandshake),
    ClientHandshake(ClientHandshake),
    ServerHandshakeResponse(ServerHandshakeResponse),
    Authenticate(Authenticate),
    AuthenticateResult(AuthenticateResult),
    Chat(Chat),
    Ready(Ready),
    ClearAllReady,
    PlayerAssignment(PlayerAssignment),
    FileTransferRequest(FileTransferRequest),
    FileTransferResponse(FileTransferResponse),
    FiletransferData(FiletransferData),
    FileTransferAck(FileTransferAck),
    JoinSyncStart(JoinSyncStart),
    Flare(Flare),
    Rejoined(Rejoined),
    Kicked(Kicked),
    ClientTimeout(ClientTimeout),
    ClientPerformance(ClientPerformance),
    ClientsLoading(ClientsLoading),
    ClientPaused(ClientPaused),
    LoadedGame(LoadedGame),
    GameStart(GameStart),
    GameSavedStart(GameSavedStart),
    EndCommandBatch(EndCommandBatch),
    SyncCheck(SyncCheck),
    SyncError(SyncError),
    AssignPlayer(AssignPlayer),
    Simulation(Simulation),
    GameSetup(GameSetup),
}

impl NetMessage {
    /// Corrresponds exactly to source/network/NetMessages.h NetMessageType enum discriminants
    pub fn id(&self) -> u8 {
        match self {
            NetMessage::ServerHandshake(_) => 1,
            NetMessage::ClientHandshake(_) => 2,
            NetMessage::ServerHandshakeResponse(_) => 3,
            NetMessage::Authenticate(_) => 4,
            NetMessage::AuthenticateResult(_) => 5,
            NetMessage::Chat(_) => 6,
            NetMessage::Ready(_) => 7,
            NetMessage::ClearAllReady => 8,
            NetMessage::GameSetup(_) => 9,
            NetMessage::AssignPlayer(_) => 10,
            NetMessage::PlayerAssignment(_) => 11,
            NetMessage::FileTransferRequest(_) => 12,
            NetMessage::FileTransferResponse(_) => 13,
            NetMessage::FiletransferData(_) => 14,
            NetMessage::FileTransferAck(_) => 15,
            NetMessage::JoinSyncStart(_) => 16,
            NetMessage::Rejoined(_) => 17,
            NetMessage::Kicked(_) => 18,
            NetMessage::ClientTimeout(_) => 19,
            NetMessage::ClientPerformance(_) => 20,
            NetMessage::ClientsLoading(_) => 21,
            NetMessage::ClientPaused(_) => 22,
            NetMessage::LoadedGame(_) => 23,
            NetMessage::GameStart(_) => 24,
            NetMessage::GameSavedStart(_) => 25,
            NetMessage::EndCommandBatch(_) => 26,
            NetMessage::SyncCheck(_) => 27,
            NetMessage::SyncError(_) => 28,
            NetMessage::Simulation(_) => 29,
            NetMessage::Flare(_) => 30,

            // Internal messages are never serialized
            NetMessage::ConnectComplete => 255,
            NetMessage::ConnectionLost => 254,
            NetMessage::Invalid => 253,
        }
    }
    pub fn name(&self) -> &'static str {
        match self {
            NetMessage::ServerHandshake(_) => "ServerHandshake",
            NetMessage::ClientHandshake(_) => "ClientHandshake",
            NetMessage::ServerHandshakeResponse(_) => "ServerHandshakeResponse",
            NetMessage::Authenticate(_) => "Authenticate",
            NetMessage::AuthenticateResult(_) => "AuthenticateResult",
            NetMessage::Chat(_) => "Chat",
            NetMessage::Ready(_) => "Ready",
            NetMessage::ClearAllReady => "ClearAllReady",
            NetMessage::GameSetup(_) => "GameSetup",
            NetMessage::AssignPlayer(_) => "AssignPlayer",
            NetMessage::PlayerAssignment(_) => "PlayerAssignment",
            NetMessage::FileTransferRequest(_) => "FileTransferRequest",
            NetMessage::FileTransferResponse(_) => "FileTransferResponse",
            NetMessage::FiletransferData(_) => "FiletransferData",
            NetMessage::FileTransferAck(_) => "FileTransferAck",
            NetMessage::JoinSyncStart(_) => "JoinSyncStart",
            NetMessage::Rejoined(_) => "Rejoined",
            NetMessage::Kicked(_) => "Kicked",
            NetMessage::ClientTimeout(_) => "ClientTimeout",
            NetMessage::ClientPerformance(_) => "ClientPerformance",
            NetMessage::ClientsLoading(_) => "ClientsLoading",
            NetMessage::ClientPaused(_) => "ClientPaused",
            NetMessage::LoadedGame(_) => "LoadedGame",
            NetMessage::GameStart(_) => "GameStart",
            NetMessage::GameSavedStart(_) => "GameSavedStart",
            NetMessage::EndCommandBatch(_) => "EndCommandBatch",
            NetMessage::SyncCheck(_) => "SyncCheck",
            NetMessage::SyncError(_) => "SyncError",
            NetMessage::Simulation(_) => "Simulation",
            NetMessage::Flare(_) => "Flare",

            // Internal messages are never serialized
            NetMessage::ConnectComplete => "ConnectComplete",
            NetMessage::ConnectionLost => "ConnectionLost",
            NetMessage::Invalid => "Invalid",
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        let body = match self {
            Self::ServerHandshake(m) => m.to_bytes(),
            Self::ClientHandshake(m) => m.to_bytes(),
            Self::ServerHandshakeResponse(m) => m.to_bytes(),
            Self::Authenticate(m) => m.to_bytes(),
            Self::AuthenticateResult(m) => m.to_bytes(),
            Self::Chat(m) => m.to_bytes(),
            Self::Ready(m) => m.to_bytes(),
            Self::ClearAllReady => vec![],
            Self::GameSetup(m) => m.to_bytes(),
            Self::AssignPlayer(m) => m.to_bytes(),
            Self::PlayerAssignment(m) => m.to_bytes(),
            Self::FileTransferRequest(m) => m.to_bytes(),
            Self::FileTransferResponse(m) => m.to_bytes(),
            Self::FiletransferData(m) => m.to_bytes(),
            Self::FileTransferAck(m) => m.to_bytes(),
            Self::JoinSyncStart(m) => m.to_bytes(),
            Self::Flare(m) => m.to_bytes(),
            Self::Rejoined(m) => m.to_bytes(),
            Self::Kicked(m) => m.to_bytes(),
            Self::ClientTimeout(m) => m.to_bytes(),
            Self::ClientPerformance(m) => m.to_bytes(),
            Self::ClientsLoading(m) => m.to_bytes(),
            Self::ClientPaused(m) => m.to_bytes(),
            Self::LoadedGame(m) => m.to_bytes(),
            Self::GameStart(m) => m.to_bytes(),
            Self::GameSavedStart(m) => m.to_bytes(),
            Self::EndCommandBatch(m) => m.to_bytes(),
            Self::SyncCheck(m) => m.to_bytes(),
            Self::SyncError(m) => m.to_bytes(),
            Self::Simulation(m) => m.to_bytes(),

            // Internal messages are never serialized
            Self::ConnectComplete => vec![],
            Self::ConnectionLost => vec![],
            Self::Invalid => vec![],
        };

        let total_size = 3 + body.len(); // header + body
        let mut bytes = vec![self.id()]; // 1 byte: message type
        bytes.extend_from_slice(&(total_size as u16).to_be_bytes()); // 2 bytes: size
        bytes.extend(body);

        if let Self::GameSetup(_) = self {
        } else {
            log::info!("\n{}", hex_dump(&bytes))
        }

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        if buffer.len() < 3 {
            return Err("Buffer too short for header".into());
        }

        let msg_type = buffer[0];

        let size = u16::from_be_bytes([buffer[1], buffer[2]]) as usize;

        if buffer.len() != size {
            return Err(format!(
                "Buffer size mismatch: declared {}, actual {}",
                size,
                buffer.len()
            ));
        }

        let body = &buffer[3..];

        match msg_type {
            1 => Ok(Self::ServerHandshake(ServerHandshake::from_bytes(body)?)),
            2 => Ok(Self::ClientHandshake(ClientHandshake::from_bytes(body)?)),
            3 => Ok(Self::ServerHandshakeResponse(
                ServerHandshakeResponse::from_bytes(body)?,
            )),
            4 => Ok(Self::Authenticate(Authenticate::from_bytes(body)?)),
            5 => Ok(Self::AuthenticateResult(AuthenticateResult::from_bytes(
                body,
            )?)),
            6 => Ok(Self::Chat(Chat::from_bytes(body)?)),
            7 => Ok(Self::Ready(Ready::from_bytes(body)?)),
            8 => Ok(Self::ClearAllReady),
            9 => Ok(Self::GameSetup(GameSetup::from_bytes(body)?)),
            10 => Ok(Self::AssignPlayer(AssignPlayer::from_bytes(body)?)),
            11 => Ok(Self::PlayerAssignment(PlayerAssignment::from_bytes(body)?)),
            12 => Ok(Self::FileTransferRequest(FileTransferRequest::from_bytes(
                body,
            )?)),
            13 => Ok(Self::FileTransferResponse(
                FileTransferResponse::from_bytes(body)?,
            )),
            14 => Ok(Self::FiletransferData(FiletransferData::from_bytes(body)?)),
            15 => Ok(Self::FileTransferAck(FileTransferAck::from_bytes(body)?)),
            16 => Ok(Self::JoinSyncStart(JoinSyncStart::from_bytes(body)?)),
            17 => Ok(Self::Rejoined(Rejoined::from_bytes(body)?)),
            18 => Ok(Self::Kicked(Kicked::from_bytes(body)?)),
            19 => Ok(Self::ClientTimeout(ClientTimeout::from_bytes(body)?)),
            20 => Ok(Self::ClientPerformance(ClientPerformance::from_bytes(
                body,
            )?)),
            21 => Ok(Self::ClientsLoading(ClientsLoading::from_bytes(body)?)),
            22 => Ok(Self::ClientPaused(ClientPaused::from_bytes(body)?)),
            23 => Ok(Self::LoadedGame(LoadedGame::from_bytes(body)?)),
            24 => Ok(Self::GameStart(GameStart::from_bytes(body)?)),
            25 => Ok(Self::GameSavedStart(GameSavedStart::from_bytes(body)?)),
            26 => Ok(Self::EndCommandBatch(EndCommandBatch::from_bytes(body)?)),
            27 => Ok(Self::SyncCheck(SyncCheck::from_bytes(body)?)),
            28 => Ok(Self::SyncError(SyncError::from_bytes(body)?)),
            29 => Ok(Self::Simulation(Simulation::from_bytes(body)?)),
            30 => Ok(Self::Flare(Flare::from_bytes(body)?)),
            _ => Err(format!("Invalid message type: {}", msg_type)),
        }
    }
}

use std::net::IpAddr;

use crate::messages::Guid;
use rusty_enet::PeerID;

/// Reasons sent by server to clients in disconnection messages.
/// Must be kept in sync with binaries/data/mods/public/gui/common/network.js
/// To avoid ambiguity, use a distinct reason for each callstack leading to a disconnect.
/// Unknown should remain reserved to the case where it is actually not a known disconnect by the server.
#[derive(Debug)]
#[repr(C)]
pub enum DisconnectReason {
    Unknown = 0,
    ConnectionRequestTimedOut,
    ServerShutdown,
    IncorrectProtocolVersion,
    ServerLoading,
    ServerAlreadyInGame,
    Kicked,
    Banned,
    PlayernameInUse,
    ServerFull,
    LobbyAuthFailed,
    GuidFailed,
    IncorrectReadyTurnCommands,
    IncorrectReadyTurnSimulated,
    ServerRefused,
    StunPortFailed,
    StunEndpointFailed,
    IncorrectSoftwareVersion,
}

/// Server session representation of client state
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum ServerSessionState {
    /// The client has disconnected or been disconnected
    Unconnected,

    /// The client has just connected and we're waiting for its handshake message,
    /// to agree on the protocol version
    Handshake,

    /// The client has handshook and we're waiting for its lobby authentication message
    LobbyAuthenticate,

    // The client has handshook and we're waiting for its authentication message,
    // to find its name and check its password etc
    Authenticate,

    /// The client has fully joined, and is in the pregame setup stage
    /// or is loading the game.
    /// Server must be in SERVER_STATE_PREGAME or SERVER_STATE_LOADING.
    Pregame,

    // The client has authenticated but the game was already started,
    // so it's synchronising with the game state from other clients
    JoinSyncing,

    /// The client is running the game.
    /// Server must be in SERVER_STATE_LOADING or SERVER_STATE_INGAME.
    Ingame,
}

/// The server's end of a network session.
/// Represents an abstraction of the state of the client, storing all the per-client data
/// needed by the server.

pub struct ServerSession {
    pub peer: PeerID,
    pub address: IpAddr,
    guid: Guid,
    user_name: String,
    host_id: u32,
    password: String,
    pub state: ServerSessionState,
}

impl ServerSession {
    pub fn new(peer: PeerID, address: IpAddr) -> Self {
        Self {
            peer,
            address,
            guid: Guid::new(),
            user_name: "TODO".to_string(),
            host_id: 0, //TODO
            password: "TODO".to_string(),
            state: ServerSessionState::Handshake,
        }
    }

    pub fn get_guid(&self) -> Guid {
        self.guid.clone()
    }
    pub fn set_guid(&mut self, guid: Guid) {
        self.guid = guid;
    }

    pub fn get_user_name(&self) -> String {
        self.user_name.clone()
    }
    pub fn set_user_name(&mut self, name: String) {
        self.user_name = name;
    }

    pub fn get_host_id(&self) -> u32 {
        self.host_id
    }
    pub fn set_host_id(&mut self, id: u32) {
        self.host_id = id;
    }
}

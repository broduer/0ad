use std::collections::HashMap;

use serde::Deserialize;
use tokio::sync::mpsc;

#[derive(Debug, Deserialize, Clone)]
pub struct XmppCredentials {
    pub jid: String,
    pub password: String,
}

#[derive(Debug, Deserialize, Clone)]
pub struct LobbyConfig {
    pub xmpp_server: String,
    pub accounts: Vec<XmppCredentials>,
    pub muc_room: String,
    pub bot_jid: String,
    pub public_ip: String,
    pub server_name: Option<String>,
    /// Engine version string used in password hashing (e.g. "0.29.0").
    /// Must match the client's `engine_version` constant.
    #[serde(default = "default_engine_version")]
    pub engine_version: String,
}

fn default_engine_version() -> String {
    "0.29.0".to_string()
}

/// XMPP task -> Game Server thread
pub enum LobbyToGameEvent {
    LobbyAuth {
        username: String,
        token: String,
    },
    /// The pre-hashed password for ENet-level authentication.
    /// Sent once after AssignGame when bound_jid is known.
    HashedPassword {
        password: String,
    },
}

/// Game Server thread -> XMPP task
pub enum GameToLobbyEvent {
    RegisterGame {
        attrs: HashMap<String, String>,
    },
    ChangeState {
        num_players: u32,
        player_names: String,
    },
    UnregisterGame,
    SendMucMessage {
        body: String,
    },
    GameEnded {
        player_names: Vec<String>,
    },
}

/// Control messages from LobbyManager to per-account XMPP task.
pub enum AccountControl {
    AssignGame {
        lobby_tx: mpsc::Sender<LobbyToGameEvent>,
        game_rx: mpsc::Receiver<GameToLobbyEvent>,
        password: String,
        port: u16,
    },
    ReleaseGame,
    /// Send a groupchat message to the MUC room.
    SendMucMessage {
        body: String,
    },
    /// Gracefully shut down this XMPP account task.
    Shutdown,
}

/// Events from XMPP account tasks to the main orchestrator loop.
pub enum XmppToMainEvent {
    /// A "hostme" command was received in MUC chat.
    HostRequested { account_id: usize, sender: String },
    /// The game channel was closed (game thread exited).
    GameEnded {
        account_id: usize,
        player_names: Vec<String>,
    },
}

pub struct LobbyGameHandle {
    pub lobby_rx: mpsc::Receiver<LobbyToGameEvent>,
    pub game_tx: mpsc::Sender<GameToLobbyEvent>,
    pub account_id: usize,
    /// Held temporarily until `activate_game` is called -- these are the XMPP-task side of the channels.
    pub game_rx: Option<mpsc::Receiver<GameToLobbyEvent>>,
    pub lobby_tx: Option<mpsc::Sender<LobbyToGameEvent>>,
}

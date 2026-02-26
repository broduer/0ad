use std::{
    collections::{HashMap, HashSet},
    net::IpAddr,
    sync::mpsc::Sender,
};

use chrono::Utc;
use rusty_enet::PeerID;

use crate::{
    file_transfer::FileTransferManager,
    messages::{
        AuthenticateResult, AuthenticateResultCode, Chat, ClientPaused, ClientsLoading, EnabledMod,
        EndCommandBatch, Guid, Host, JoinSyncStart, LoadedGame, NetMessage, PlayerAssignment,
        Ready, Rejoined, ServerHandshakeResponse, Simulation,
    },
    network_message::{InboundNetworkMessage, OutboundNetworkMessage},
    server_session::{DisconnectReason, ServerSession, ServerSessionState},
    turn_manager::{TurnManager, TurnManagerAction},
    utils::{handshake_is_correct, handshake_matches_controller, hex_dump},
};

const PS_PROTOCOL_MAGIC: u32 = 0x5073013f;
const PS_PROTOCOL_VERSION: u32 = 0x01010019;

const MAX_CLIENTS: usize = 41;

fn strip_lobby_suffix(name: &str) -> &str {
    name.split(" (").next().unwrap_or(name)
}

fn server_handshake_default() -> crate::messages::ServerHandshake {
    crate::messages::ServerHandshake {
        magic: PS_PROTOCOL_MAGIC,
        protocol_version: PS_PROTOCOL_VERSION,
        engine_version: "0.0.0".to_string(),
        enabled_mods: vec![],
    }
}

#[derive(Debug, PartialEq)]
pub enum ServerState {
    /// We haven't opened the port yet, we're just setting some stuff up.
    /// The worker thread has not been started.
    Unconnected,
    /// The server is open and accepting connections. This is the screen where
    /// rules are set up by the operator and where players join and select civs
    /// and stuff.struct Started;
    Pregame,
    /// All the hosts are connected and are loading the game
    Loading,
    /// The one with all the killing ;-)
    InGame,
    /// The game is over and someone has won. Players might linger to chat or
    /// download the replay log.
    PostGame,
}

pub struct Server {
    network_sink: Sender<OutboundNetworkMessage>,
    public_port: u16,
    port_label: String,
    public_ip: IpAddr,
    password: String,
    failed_attempts: HashMap<String, u32>,
    state: ServerState,

    pub(crate) player_assignments: HashMap<Guid, ServerPlayerAssignment>,

    /// Raw init attributes bytes, received verbatim from the GameStart message.
    /// Treated as opaque bytes -- never parsed or re-serialized by the server.
    /// Not meaningful until the server is LOADING or INGAME.
    pub(crate) init_attributes_raw: Vec<u8>,

    /**
     * Whether this match continues a saved game.
     */
    pub(crate) continues_saved_game: bool,

    /**
     * Whether this match requires lobby authentication.
     */
    pub(crate) lobby_auth: bool,

    pub(crate) sessions: Vec<ServerSession>,

    pub(crate) stats: String, // TODO

    pub(crate) server_name: String,

    pub(crate) banned_ips: Vec<IpAddr>,

    pub(crate) banned_players: Vec<String>,

    /// Holds the GUIDs of all currently paused players.
    pub(crate) pausing_players: Vec<String>,

    pub(crate) next_host_id: u32,

    pub(crate) server_turn_manager: TurnManager,

    /// The GUID of the client in control of the game (the 'host' from the players' perspective).
    pub(crate) controller_guid: Option<Guid>,

    /// Engine version and mod list reported by the controller during handshake.
    /// All subsequent clients are validated against this.
    controller_handshake: Option<(String, Vec<EnabledMod>)>,

    /// The 'secret' used to identify the controller of the game.
    pub(crate) controller_secret: String,

    /// A copy of all simulation commands received so far, indexed by
    /// turn number, to simplify support for rejoining etc.
    /// TODO: verify this doesn't use too much RAM.
    pub(crate) saved_commands: Vec<Vec<Simulation>>,

    /// The latest copy of the simulation state, received from an existing
    /// client when a new client has asked to rejoin the game.
    pub(crate) join_sync_file: Vec<u8>,

    /// Manages file transfer relay for rejoin: receives state from a source
    /// client and sends it on to the rejoining client.
    file_transfer_manager: FileTransferManager,

    /// The loaded game data when a game is loaded.
    pub(crate) saved_state: Option<Vec<u8>>,

    /// Time when the clients connections were last checked for timeouts and latency.
    pub(crate) last_connection_check: chrono::DateTime<Utc>,

    /// Receiver for lobby events (lobby auth tokens from XMPP).
    lobby_rx: Option<tokio::sync::mpsc::Receiver<crate::lobby::types::LobbyToGameEvent>>,

    /// Sender for game lifecycle events to the XMPP lobby task.
    pub(crate) lobby_tx: Option<tokio::sync::mpsc::Sender<crate::lobby::types::GameToLobbyEvent>>,

    /// Whether the game has been registered with the lobby.
    lobby_registered: bool,

    /// Last time the game was registered/re-registered with the lobby.
    last_lobby_register: std::time::Instant,

    /// Whether any client has ever connected to this game (for auto-shutdown).
    has_had_clients: bool,

    /// When this server was created (for first-connection timeout).
    created_at: chrono::DateTime<Utc>,

    /// Names of all players who successfully authenticated during this session.
    authenticated_players: Vec<String>,

    /// Player IDs that have sent a "resign" simulation command.
    resigned_players: HashSet<i32>,

    /// When the game transitioned to InGame state (for duration reporting).
    game_start_time: Option<chrono::DateTime<Utc>>,

    /// Cached from init_attributes at game start.
    cheats_enabled: bool,
    is_rated: bool,
}

impl Server {
    pub fn new(
        network_sink: Sender<OutboundNetworkMessage>,
        is_saved_game: bool,
        use_lobby_auth: Option<bool>,
        public_port: u16,
        public_ip: IpAddr,
        password: String,
        server_name: String,
        lobby_rx: Option<tokio::sync::mpsc::Receiver<crate::lobby::types::LobbyToGameEvent>>,
        lobby_tx: Option<tokio::sync::mpsc::Sender<crate::lobby::types::GameToLobbyEvent>>,
    ) -> Self {
        let port_label = public_port.to_string();
        crate::metrics::GAME_STATE
            .with_label_values(&[&port_label])
            .set(0.0); // Pregame

        Self {
            network_sink,
            public_port,
            port_label,
            public_ip,
            password,
            failed_attempts: HashMap::new(),
            state: ServerState::Pregame,
            player_assignments: HashMap::new(),
            init_attributes_raw: Vec::new(),
            continues_saved_game: is_saved_game,
            lobby_auth: use_lobby_auth.unwrap_or_default(),
            sessions: Vec::new(),
            stats: "TODO".to_string(),
            server_name,
            banned_ips: Vec::new(),
            banned_players: Vec::new(),
            pausing_players: Vec::new(),
            next_host_id: 1,
            server_turn_manager: TurnManager::new(),
            controller_guid: None,
            controller_handshake: None,
            controller_secret: "TODO".to_string(),
            saved_commands: Vec::new(),
            join_sync_file: Vec::new(),
            file_transfer_manager: FileTransferManager::new(),
            saved_state: None,
            last_connection_check: Utc::now(),
            lobby_rx,
            lobby_tx,
            lobby_registered: false,
            last_lobby_register: std::time::Instant::now(),
            has_had_clients: false,
            created_at: Utc::now(),
            authenticated_players: Vec::new(),
            resigned_players: HashSet::new(),
            game_start_time: None,
            cheats_enabled: false,
            is_rated: false,
        }
    }

    pub fn start(&mut self, event_rx: std::sync::mpsc::Receiver<InboundNetworkMessage>) {
        // Register with the lobby immediately so the game is visible before any client connects.
        if self.lobby_tx.is_some() && !self.lobby_registered {
            let attrs = self.build_lobby_game_attrs(None);
            self.send_lobby_register_game(attrs);
            self.lobby_registered = true;
        }

        loop {
            if !self.has_had_clients && (Utc::now() - self.created_at).num_seconds() >= 60 {
                log::info!("No client connected within 60s timeout, shutting down");
                self.send_lobby_unregister_game();
                return;
            }

            // 1. Drain lobby events FIRST (lobby auth tokens from XMPP)
            //    Must run before network messages so the lobby auth prompt reaches the
            //    client before its Authenticate message is processed.
            //    Take the receiver out temporarily to avoid double-borrow of `self`.
            if let Some(mut lobby_rx) = self.lobby_rx.take() {
                while let Ok(evt) = lobby_rx.try_recv() {
                    match evt {
                        crate::lobby::types::LobbyToGameEvent::LobbyAuth { username, token } => {
                            self.process_lobby_auth(&username, &token);
                        }
                        crate::lobby::types::LobbyToGameEvent::HashedPassword { password } => {
                            log::info!("Received pre-hashed password from XMPP layer");
                            self.password = password;
                        }
                    }
                }
                self.lobby_rx = Some(lobby_rx);
            }

            // 2. Drain all pending network messages (non-blocking)
            loop {
                match event_rx.try_recv() {
                    Ok(msg) => self.handle_event(msg),
                    Err(std::sync::mpsc::TryRecvError::Empty) => break,
                    Err(std::sync::mpsc::TryRecvError::Disconnected) => {
                        log::info!("Server event channel disconnected, shutting down");
                        return;
                    }
                }
            }

            // 3. Auto-shutdown when the last client disconnects
            if self.has_had_clients && self.sessions.is_empty() {
                log::info!("Last client disconnected, shutting down game server");
                self.send_lobby_unregister_game();
                if let Some(ref tx) = self.lobby_tx {
                    let _ = tx.try_send(crate::lobby::types::GameToLobbyEvent::GameEnded {
                        player_names: self.authenticated_players.clone(),
                    });
                }
                return;
            }

            // 4. Periodically re-register with the lobby so the game stays visible
            if self.lobby_registered
                && self.last_lobby_register.elapsed() >= std::time::Duration::from_secs(30)
            {
                let attrs = self.build_lobby_game_attrs(None);
                self.send_lobby_register_game(attrs);
            }

            // 5. Sleep a short interval to avoid busy-looping
            std::thread::sleep(chrono::Duration::milliseconds(10).to_std().unwrap());
        }
    }

    fn handle_event(&mut self, event: InboundNetworkMessage) {
        match event {
            InboundNetworkMessage::Connect { ip, peer } => self.handle_connect(peer, ip),
            InboundNetworkMessage::Disconnect {
                ip,
                peer,
                reason: _,
            } => {
                log::warn!("Peer disconnected: {ip}");

                let session_idx = self
                    .sessions
                    .iter()
                    .position(|s| s.peer == peer)
                    .expect("peer should be known at this point");

                let state = self.sessions[session_idx].state;
                if matches!(
                    state,
                    ServerSessionState::Pregame
                        | ServerSessionState::JoinSyncing
                        | ServerSessionState::Ingame
                ) {
                    // on_disconnect calls on_user_leave; session state is still original here
                    // (not overwritten by FSM), so the JoinSyncing check in on_user_leave works.
                    self.on_disconnect(session_idx);
                } else {
                    self.sessions.remove(session_idx);
                }
            }
            InboundNetworkMessage::Message { ip: _, peer, data } => {
                let session_idx = self
                    .sessions
                    .iter()
                    .position(|s| s.peer == peer)
                    .expect("peer should be known at this point");

                log::info!("Received message:\n{}", hex_dump(&data));

                let event = NetMessage::from_bytes(&data).expect("cannot deserialize message");
                log::info!("Received message: {}", event.name());
                crate::metrics::MESSAGES_RECEIVED_TOTAL
                    .with_label_values(&[&self.port_label, event.name()])
                    .inc();

                // File transfer messages are handled outside the FSM, mirroring the C++ server.
                if FileTransferManager::is_file_transfer_message(&event) {
                    self.handle_file_transfer_message(peer, event);
                    return;
                }

                self.handle_session_event(session_idx, event);
            }
        }
    }

    /// Handle file transfer messages outside the FSM, mirroring C++ HandleMessageReceive.
    fn handle_file_transfer_message(&mut self, peer: PeerID, event: NetMessage) {
        match event {
            // Source client sends back the game state (response header with total size)
            NetMessage::FileTransferResponse(msg) => {
                if let Err(e) = self.file_transfer_manager.handle_response(&msg) {
                    log::warn!("FileTransferResponse error: {}", e);
                }
            }
            // Source client sends a data chunk
            NetMessage::FiletransferData(msg) => {
                match self.file_transfer_manager.handle_data(peer, &msg) {
                    Err(e) => log::warn!("FiletransferData error: {}", e),
                    Ok((ack_msg, completed)) => {
                        // Always ack the data packet back to the source client
                        self.send(ack_msg);

                        if let Some((target_host_id, buffer)) = completed {
                            // Transfer from source is complete -- store and notify the rejoiner
                            self.join_sync_file = buffer;

                            // Find the rejoining session by host_id
                            let rejoiner_peer = self
                                .sessions
                                .iter()
                                .find(|s| s.get_host_id() == target_host_id)
                                .map(|s| s.peer);

                            if let Some(rejoiner_peer) = rejoiner_peer {
                                let join_sync = NetMessage::JoinSyncStart(JoinSyncStart {
                                    init_attributes: self.init_attributes_raw.clone(),
                                });
                                self.send(OutboundNetworkMessage::Message {
                                    peer: rejoiner_peer,
                                    data: join_sync.to_bytes(),
                                });
                                log::info!(
                                    "Sent JoinSyncStart to rejoining peer {:?} ({} bytes of state)",
                                    rejoiner_peer,
                                    self.join_sync_file.len()
                                );
                            } else {
                                log::warn!(
                                    "Rejoining client host_id={} disconnected before we could send JoinSyncStart",
                                    target_host_id
                                );
                            }
                        }
                    }
                }
            }
            // Rejoining client requests the state file
            NetMessage::FileTransferRequest(msg) => {
                if self.join_sync_file.is_empty() {
                    log::warn!(
                        "FileTransferRequest from peer {:?} but join_sync_file is empty",
                        peer
                    );
                    return;
                }
                let data = self.join_sync_file.clone();
                let msgs = self.file_transfer_manager.handle_request(peer, &msg, &data);
                for m in msgs {
                    self.send(m);
                }
            }
            // Acks from the source client (ignored; we don't use windowing on the receive side)
            NetMessage::FileTransferAck(_) => {}
            _ => unreachable!("non-file-transfer message routed to handle_file_transfer_message"),
        }
    }

    fn handle_connect(&mut self, peer: PeerID, address: IpAddr) {
        log::info!("Connect from ip: {address}");
        self.has_had_clients = true;
        self.sessions.push(ServerSession::new(peer, address));
        crate::metrics::CONNECTED_CLIENTS
            .with_label_values(&[&self.port_label])
            .inc();

        if self.banned_ips.contains(&address) {
            log::info!("Rejected banned IP: {address}");
            self.send(OutboundNetworkMessage::Disconnect {
                peer,
                reason: DisconnectReason::Banned as u32,
            });
            return;
        }

        // If a controller is already established, advertise their engine version and
        // mods so that mismatched clients can identify what differs before disconnecting.
        let handshake = NetMessage::ServerHandshake(match &self.controller_handshake {
            Some((engine_version, mods)) => crate::messages::ServerHandshake {
                magic: PS_PROTOCOL_MAGIC,
                protocol_version: PS_PROTOCOL_VERSION,
                engine_version: engine_version.clone(),
                enabled_mods: mods.clone(),
            },
            None => server_handshake_default(),
        });

        self.send(OutboundNetworkMessage::Message {
            peer,
            data: handshake.to_bytes(),
        });
    }
    pub fn use_lobby_auth(&self) -> bool {
        self.lobby_auth
    }

    /// Process a lobby auth event: find the session whose GUID matches the token,
    /// set the username from XMPP, and send an empty Authenticate message to prompt
    /// the client to proceed. Mirrors C++ ProcessLobbyAuth (NetServer.cpp:889).
    fn process_lobby_auth(&mut self, name: &str, token: &str) {
        log::info!(
            "Lobby auth: received token for user '{}' with token '{}'",
            name,
            token
        );

        let session_idx = self.sessions.iter().position(|s| s.get_guid().0 == token);

        let Some(session_idx) = session_idx else {
            log::warn!(
                "Lobby auth: no session found with GUID matching token '{}'",
                token
            );
            return;
        };

        // Only send the auth prompt if the session is still waiting for lobby auth.
        // If it already advanced (e.g. race condition), skip sending to avoid FSM error.
        if self.sessions[session_idx].state != ServerSessionState::LobbyAuthenticate {
            log::warn!(
                "Lobby auth: session for '{}' already in state {:?}, skipping auth prompt",
                name,
                self.sessions[session_idx].state
            );
            return;
        }

        self.sessions[session_idx].set_user_name(name.to_string());

        // Send an empty Authenticate message to prompt the client to send its
        // real Authenticate message now that its identity is verified via the lobby.
        let empty_auth = NetMessage::Authenticate(crate::messages::Authenticate {
            name: String::new(),
            password: String::new(),
            controller_secret: String::new(),
        });

        self.send(OutboundNetworkMessage::Message {
            peer: self.sessions[session_idx].peer,
            data: empty_auth.to_bytes(),
        });

        // Transition to Authenticate so the client's response is handled correctly.
        self.sessions[session_idx].state = ServerSessionState::Authenticate;
    }

    /// Return the serving port on the local machine.
    pub fn get_local_port(&self) -> u16 {
        self.public_port
    }

    /// Switch in game mode. The clients will have to be notified to start the
    /// game. This method is called by StartGame and StartSavedGame
    fn pre_start_game(&mut self, init_attribs: &[u8]) {
        // Validate that all enabled players (not observers) are ready.
        for (guid, pa) in &self.player_assignments {
            if pa.enabled && pa.player_id != -1 && pa.status == 0 {
                log::warn!(
                    "Tried to start the game without player \"{}\" being ready!",
                    pa.name
                );
                return;
            }
        }

        self.init_attributes_raw = init_attribs.to_vec();

        // Parse init_attributes once and cache game-lifetime values.
        let parsed = serde_json::from_slice::<serde_json::Value>(init_attribs).ok();
        if let Some(ref root) = parsed {
            let settings = root.get("settings");
            self.cheats_enabled = settings
                .and_then(|s| s.get("CheatsEnabled"))
                .and_then(|v| v.as_bool())
                .unwrap_or(false);
            self.is_rated = settings
                .and_then(|s| s.get("RatingEnabled"))
                .and_then(|v| v.as_bool())
                .unwrap_or(false);
        }

        self.state = ServerState::Loading;
        crate::metrics::GAME_STATE
            .with_label_values(&[&self.port_label])
            .set(1.0); // Loading

        self.send_lobby_changestate();

        // Phase B: re-register with full map details parsed from init_attributes (JSON)
        if self.lobby_tx.is_some() {
            let map_overrides = parsed.map(|root| {
                let mut overrides = HashMap::new();

                if let Some(map_name) = root.get("map").and_then(|v| v.as_str()) {
                    overrides.insert("mapName".to_string(), map_name.to_string());
                    overrides.insert("niceMapName".to_string(), map_name.to_string());
                }

                if let Some(map_type) = root.get("mapType").and_then(|v| v.as_str()) {
                    overrides.insert("mapType".to_string(), map_type.to_string());

                    if let Some(size) = root.get("settings").and_then(|s| s.get("Size")) {
                        if map_type == "random" {
                            overrides.insert("mapSize".to_string(), size.to_string());
                        } else {
                            overrides.insert("mapSize".to_string(), "Default".to_string());
                        }
                    }
                }

                if let Some(conditions) = root
                    .get("settings")
                    .and_then(|s| s.get("VictoryConditions"))
                    .and_then(|v| v.as_array())
                {
                    let vc: Vec<&str> = conditions.iter().filter_map(|c| c.as_str()).collect();
                    overrides.insert("victoryConditions".to_string(), vc.join(","));
                }

                if let Some(player_data) = root
                    .get("settings")
                    .and_then(|s| s.get("PlayerData"))
                    .and_then(|v| v.as_array())
                {
                    overrides.insert("maxnbp".to_string(), player_data.len().to_string());
                }

                overrides
            });

            let attrs = self.build_lobby_game_attrs(map_overrides);
            self.send_lobby_register_game(attrs);
            self.lobby_registered = true;
        }
    }

    /// Make a player name 'nicer' by limiting the length and removing forbidden characters etc.
    fn sanitise_player_name(original: &str) -> String {
        const MAX_LENGTH: usize = 32;

        // Replace GUI tag characters for symmetry
        let mut name = original.replace('[', "{").replace(']', "}");

        // Restrict length (by chars, not bytes)
        let char_count = name.chars().count();
        if char_count > MAX_LENGTH {
            name = name.chars().take(MAX_LENGTH).collect();
        }

        let name = name.trim().to_string();

        // Don't allow empty name
        if name.is_empty() {
            "Anonymous".to_string()
        } else {
            name
        }
    }

    /// Make a player name unique, if it matches any existing session's name.
    fn deduplicate_player_name(&self, original: &str) -> String {
        let mut name = original.to_string();
        let mut id: usize = 2;
        loop {
            let unique = !self.sessions.iter().any(|s| s.get_user_name() == name);
            if unique {
                return name;
            }
            name = format!("{} ({})", original, id);
            id += 1;
        }
    }

    fn add_player(&mut self, new_guid: Guid, name: String) {
        // Find all player IDs in active use; we mustn't give them
        // to a second player (excluding the unassigned ID: -1)
        let mut user_ids = HashSet::<i32>::new();
        for player_assignment in self.player_assignments.values() {
            if player_assignment.enabled && player_assignment.player_id != -1 {
                user_ids.insert(player_assignment.player_id);
            }
        }

        // If the player is rejoining after disconnecting, try to give them
        // back their old player ID. Don't do this in pregame however,
        // as that ID might be invalid for various reasons.
        let mut player_id = -1;
        let mut found = false;
        if self.state != ServerState::Unconnected && self.state != ServerState::Pregame {
            // Try to match GUID first
            for (guid, player_assignment) in &self.player_assignments {
                if !player_assignment.enabled
                    && new_guid == *guid
                    && !user_ids.contains(&player_assignment.player_id)
                {
                    // FOUND!
                    player_id = player_assignment.player_id;
                    found = true;
                }
            }

            // Try to match username next
            if !found {
                for player_assignment in self.player_assignments.values() {
                    if !player_assignment.enabled
                        && name == player_assignment.name
                        && !user_ids.contains(&player_assignment.player_id)
                    {
                        // FOUND!
                        player_id = player_assignment.player_id;
                        found = true;
                    }
                }
            }
        }

        log::info!("PLayer name read: {name}");

        let player_assignment = ServerPlayerAssignment {
            enabled: true,
            name,
            player_id,
            status: 0,
        };

        self.player_assignments.insert(new_guid, player_assignment);

        self.send_player_assignments();
        self.update_player_metrics();
    }

    fn remove_player(&mut self, guid: &Guid) {
        if let Some(assignment) = self.player_assignments.get_mut(guid) {
            assignment.enabled = false;
        }
        self.send_player_assignments();
        self.update_player_metrics();

        // Unregister from lobby when no enabled players remain
        if self.lobby_registered && self.player_assignments.values().all(|pa| !pa.enabled) {
            self.send_lobby_unregister_game();
            self.lobby_registered = false;
        }
    }
    fn update_player_metrics(&self) {
        let total = self
            .player_assignments
            .values()
            .filter(|pa| pa.enabled && pa.player_id != -1)
            .count();
        let ready = self
            .player_assignments
            .values()
            .filter(|pa| pa.enabled && pa.status > 0)
            .count();
        crate::metrics::PLAYERS_TOTAL
            .with_label_values(&[&self.port_label])
            .set(total as f64);
        crate::metrics::PLAYERS_READY
            .with_label_values(&[&self.port_label])
            .set(ready as f64);
    }

    fn send_player_assignments(&mut self) {
        let message = NetMessage::PlayerAssignment(self.construct_player_assignment_message());
        self.multicast(
            message,
            vec![
                ServerSessionState::Pregame,
                ServerSessionState::JoinSyncing,
                ServerSessionState::Ingame,
            ],
        );

        // Notify lobby of player count/name changes
        self.send_lobby_changestate();

        // Phase A: register game with lobby once we have controller handshake data
        if !self.lobby_registered && self.controller_handshake.is_some() && self.lobby_tx.is_some()
        {
            let attrs = self.build_lobby_game_attrs(None);
            self.send_lobby_register_game(attrs);
            self.lobby_registered = true;
        }
    }

    fn handle_session_event(&mut self, session_id: usize, event: NetMessage) {
        match (self.sessions[session_id].state, event) {
            (ServerSessionState::Unconnected, NetMessage::ConnectionLost) => {
                self.sessions[session_id].state = ServerSessionState::Unconnected;
            }
            (ServerSessionState::Handshake, NetMessage::ConnectionLost) => {
                self.sessions[session_id].state = ServerSessionState::Unconnected;
            }
            (ServerSessionState::Handshake, NetMessage::ClientHandshake(m)) => {
                self.sessions[session_id].state = ServerSessionState::Authenticate;
                self.on_client_handshake(session_id, m);
            }
            (ServerSessionState::LobbyAuthenticate, _msg @ NetMessage::ConnectionLost) => {
                self.sessions[session_id].state = ServerSessionState::Unconnected;
            }
            (ServerSessionState::LobbyAuthenticate, NetMessage::Authenticate(_m)) => {
                // Client sent Authenticate before lobby auth completed -- ignore.
                // The lobby auth flow will transition to Authenticate state, then the
                // client's NMT_AUTHENTICATE prompt will trigger another Authenticate message.
                log::warn!(
                    "Ignoring Authenticate from session {} -- still waiting for lobby auth",
                    session_id
                );
            }
            (ServerSessionState::Authenticate, NetMessage::ConnectionLost) => {
                self.sessions[session_id].state = ServerSessionState::Unconnected;
            }
            (ServerSessionState::Authenticate, NetMessage::Authenticate(m)) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_authenticate(session_id, m);
            }

            // PREGAME
            (ServerSessionState::Pregame, NetMessage::ConnectionLost) => {
                self.sessions[session_id].state = ServerSessionState::Unconnected;
                self.on_disconnect(session_id);
            }
            (ServerSessionState::Pregame, NetMessage::Chat(m)) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_chat(session_id, m);
            }
            (ServerSessionState::Pregame, NetMessage::Ready(m)) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_ready(session_id, m);
            }
            (ServerSessionState::Pregame, NetMessage::ClearAllReady) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_clear_all_ready(session_id);
            }
            (ServerSessionState::Pregame, NetMessage::GameSetup(m)) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_game_setup(session_id, m);
            }
            (ServerSessionState::Pregame, NetMessage::AssignPlayer(m)) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_assign_player(session_id, m);
            }
            (ServerSessionState::Pregame, NetMessage::Kicked(m)) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_kick_player(session_id, m);
            }
            (ServerSessionState::Pregame, NetMessage::GameStart(m)) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_game_start(session_id, m);
            }
            (ServerSessionState::Pregame, NetMessage::GameSavedStart(m)) => {
                self.sessions[session_id].state = ServerSessionState::Pregame;
                self.on_saved_game_start(session_id, m);
            }
            (ServerSessionState::Pregame, NetMessage::LoadedGame(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_loaded_game(session_id, m);
            }

            // Join Syncing
            (ServerSessionState::JoinSyncing, NetMessage::Kicked(m)) => {
                self.sessions[session_id].state = ServerSessionState::JoinSyncing;
                self.on_kick_player(session_id, m);
            }
            (ServerSessionState::JoinSyncing, NetMessage::ConnectionLost) => {
                self.sessions[session_id].state = ServerSessionState::Unconnected;
                self.on_disconnect(session_id);
            }
            (ServerSessionState::JoinSyncing, NetMessage::LoadedGame(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_join_syncing_loaded_game(session_id, m);
            }

            // Ingame
            (ServerSessionState::Ingame, NetMessage::Rejoined(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_rejoined(session_id, m);
            }
            (ServerSessionState::Ingame, NetMessage::Kicked(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_kick_player(session_id, m);
            }
            (ServerSessionState::Ingame, NetMessage::ClientPaused(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_client_paused(session_id, m);
            }
            (ServerSessionState::Ingame, NetMessage::ConnectionLost) => {
                self.sessions[session_id].state = ServerSessionState::Unconnected;
                self.on_disconnect(session_id);
            }
            (ServerSessionState::Ingame, NetMessage::Chat(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_chat(session_id, m);
            }
            (ServerSessionState::Ingame, NetMessage::Simulation(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_simulation_command(session_id, m);
            }
            (ServerSessionState::Ingame, NetMessage::Flare(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_flare(session_id, m);
            }
            (ServerSessionState::Ingame, NetMessage::SyncCheck(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_sync_check(session_id, m);
            }
            (ServerSessionState::Ingame, NetMessage::EndCommandBatch(m)) => {
                self.sessions[session_id].state = ServerSessionState::Ingame;
                self.on_end_command_batch(session_id, m);
            }

            (from, event) => {
                log::warn!("Invalid transition from {from:?} to {event:?}");
            }
        }
    }

    fn construct_player_assignment_message(&mut self) -> PlayerAssignment {
        let mut hosts = Vec::new();
        for (guid, player) in &self.player_assignments {
            if !player.enabled {
                continue;
            }

            hosts.push(Host {
                guid: guid.clone(),
                name: player.name.clone(),
                player_id: player.player_id as i8,
                status: player.status,
            });
        }
        PlayerAssignment { hosts }
    }

    fn on_user_join(&mut self, session_id: usize) {
        self.add_player(
            self.sessions[session_id].get_guid(),
            self.sessions[session_id].get_user_name(),
        );

        let message = NetMessage::PlayerAssignment(self.construct_player_assignment_message());
        self.send(OutboundNetworkMessage::Message {
            peer: self.sessions[session_id].peer,
            data: message.to_bytes(),
        });
    }
    fn on_user_leave(&mut self, session_id: usize) {
        let guid = self.sessions[session_id].get_guid();
        let state = self.sessions[session_id].state;
        let host_id = self.sessions[session_id].get_host_id();

        // Mark as Unconnected first so multicast (send_player_assignments) skips this peer.
        // Mirrors C++ where the session was removed from m_Sessions before OnUserLeave ran.
        self.sessions[session_id].state = ServerSessionState::Unconnected;

        // Mirror C++: remove from pausing players list
        self.pausing_players.retain(|g| *g != guid.to_string());

        // Mirror C++: RemovePlayer -- mark disabled, broadcast updated assignments
        self.remove_player(&guid);

        // Mirror C++: uninitialise turn manager, except for join-syncing clients
        // (they haven't been registered with the turn manager yet).
        if state != ServerSessionState::JoinSyncing {
            let actions = self.server_turn_manager.uninitialise_client(host_id);
            self.process_turn_manager_actions(actions);
        }
    }

    fn send(&mut self, msg: OutboundNetworkMessage) {
        if let OutboundNetworkMessage::Message { peer: _, data } = &msg {
            let event = NetMessage::from_bytes(data).expect("cannot deserialize message");
            log::info!("Sending message: {}", event.name());
        }

        self.network_sink.send(msg).unwrap();

        log::info!("PA: {:?}", self.player_assignments);
    }

    fn disconnect_session(&mut self, session_id: usize, reason: DisconnectReason) {
        self.send(OutboundNetworkMessage::Disconnect {
            peer: self.sessions[session_id].peer,
            reason: reason as u32,
        });
    }

    fn is_controller(&self, session_id: usize) -> bool {
        self.controller_guid
            .as_ref()
            .is_some_and(|cg| cg == &self.sessions[session_id].get_guid())
    }

    fn is_observer(&self, guid: &Guid) -> bool {
        self.player_assignments
            .get(guid)
            .map(|pa| pa.player_id == -1)
            .unwrap_or(true)
            && self.controller_guid.as_ref() != Some(guid)
    }

    /// Build the JSON mods string expected by the lobby from the controller handshake.
    fn build_mods_json(&self) -> String {
        let Some((_, ref mods)) = self.controller_handshake else {
            return r#"[{"mod":"public","name":"0ad","version":"0.29.0","ignoreInCompatibilityChecks":false}]"#.to_string();
        };
        let mods_array: Vec<serde_json::Value> = mods
            .iter()
            .map(|m| {
                serde_json::json!({
                    "mod": "public",
                    "name": m.name,
                    "version": m.version,
                    "ignoreInCompatibilityChecks": false,
                })
            })
            .collect();
        serde_json::to_string(&mods_array).unwrap_or_else(|_| "[]".to_string())
    }

    /// Build the lobby game attribute map from current server state.
    /// `map_overrides` can supply map-related attributes when available (at game start).
    fn build_lobby_game_attrs(
        &self,
        map_overrides: Option<HashMap<String, String>>,
    ) -> HashMap<String, String> {
        let active: Vec<&ServerPlayerAssignment> = self
            .player_assignments
            .values()
            .filter(|pa| pa.enabled)
            .collect();

        let nbp = active.len().to_string();
        let player_names: String = active
            .iter()
            .map(|pa| pa.name.as_str())
            .collect::<Vec<_>>()
            .join(", ");

        let host_username = self
            .controller_guid
            .as_ref()
            .and_then(|g| self.player_assignments.get(g))
            .map(|pa| pa.name.clone())
            .unwrap_or_default();

        let mut attrs = HashMap::new();
        attrs.insert("name".to_string(), self.server_name.clone());
        attrs.insert("hostUsername".to_string(), host_username);
        attrs.insert("mods".to_string(), self.build_mods_json());
        attrs.insert(
            "hasPassword".to_string(),
            if self.password.is_empty() {
                "".to_string()
            } else {
                "true".to_string()
            },
        );
        attrs.insert("nbp".to_string(), nbp);
        attrs.insert("maxnbp".to_string(), "2".to_string());
        attrs.insert("players".to_string(), player_names);
        attrs.insert(
            "mapName".to_string(),
            "maps/skirmishes/alpine_valleys_2p".to_string(),
        );
        attrs.insert("niceMapName".to_string(), "Alpine Valleys (2)".to_string());
        attrs.insert("mapSize".to_string(), "0".to_string());
        attrs.insert("mapType".to_string(), "skirmish".to_string());
        attrs.insert("victoryConditions".to_string(), String::new());

        if let Some(overrides) = map_overrides {
            attrs.extend(overrides);
        }

        attrs
    }

    fn send_lobby_register_game(&mut self, attrs: HashMap<String, String>) {
        if let Some(ref tx) = self.lobby_tx {
            let _ = tx.try_send(crate::lobby::types::GameToLobbyEvent::RegisterGame { attrs });
        }
        self.last_lobby_register = std::time::Instant::now();
    }

    fn send_lobby_changestate(&self) {
        if let Some(ref tx) = self.lobby_tx {
            let active: Vec<&str> = self
                .player_assignments
                .values()
                .filter(|pa| pa.enabled)
                .map(|pa| pa.name.as_str())
                .collect();
            let num_players = active.len() as u32;
            let player_names = active.join(", ");
            let _ = tx.try_send(crate::lobby::types::GameToLobbyEvent::ChangeState {
                num_players,
                player_names,
            });
        }
    }

    fn send_lobby_unregister_game(&self) {
        if let Some(ref tx) = self.lobby_tx {
            let _ = tx.try_send(crate::lobby::types::GameToLobbyEvent::UnregisterGame);
        }
    }

    fn send_lobby_muc_message(&self, body: String) {
        if let Some(ref tx) = self.lobby_tx {
            let _ = tx.try_send(crate::lobby::types::GameToLobbyEvent::SendMucMessage { body });
        }
    }

    // Send a message to all clients who match one of the given states.
    fn multicast(&mut self, msg: NetMessage, session_statuses: Vec<ServerSessionState>) {
        let serialized = msg.to_bytes();

        let mut peers = Vec::new();
        for session in self.sessions.iter() {
            if session_statuses.contains(&session.state) {
                peers.push(session.peer);
            }
        }

        for peer in peers {
            self.send(OutboundNetworkMessage::Message {
                peer,
                data: serialized.clone(),
            });
        }
    }

    fn process_turn_manager_actions(&mut self, actions: Vec<TurnManagerAction>) {
        for action in actions {
            match action {
                TurnManagerAction::BroadcastEndCommandBatch { turn, turn_length } => {
                    crate::metrics::CURRENT_TURN
                        .with_label_values(&[&self.port_label])
                        .set(turn as f64);
                    let msg = NetMessage::EndCommandBatch(crate::messages::EndCommandBatch {
                        turn,
                        turn_length,
                    });
                    self.multicast(msg, vec![ServerSessionState::Ingame]);
                }
                TurnManagerAction::BroadcastSyncError {
                    turn,
                    hash_expected,
                    player_names,
                } => {
                    crate::metrics::OOS_ERRORS_TOTAL
                        .with_label_values(&[&self.port_label])
                        .inc();
                    let msg = NetMessage::SyncError(crate::messages::SyncError {
                        turn,
                        hash_expected,
                        player_names,
                    });
                    self.multicast(msg, vec![ServerSessionState::Ingame]);
                }
                TurnManagerAction::DisconnectClient { host_id, reason } => {
                    let peer = self
                        .sessions
                        .iter()
                        .find(|s| s.get_host_id() == host_id)
                        .map(|s| s.peer);
                    if let Some(peer) = peer {
                        self.send(OutboundNetworkMessage::Disconnect {
                            peer,
                            reason: reason as u32,
                        });
                    } else {
                        log::warn!(
                            "TurnManager requested disconnect for host_id {} but no session found",
                            host_id
                        );
                    }
                }
            }
        }
    }

    fn on_client_handshake(
        &mut self,
        session_id: usize,
        message: crate::messages::ClientHandshake,
    ) {
        log::debug!("Client {} handshake", self.sessions[session_id].peer.0);

        if PS_PROTOCOL_VERSION != message.protocol_version {
            self.disconnect_session(session_id, DisconnectReason::IncorrectProtocolVersion);
            return;
        }

        if !handshake_is_correct(&server_handshake_default(), &message) {
            self.disconnect_session(session_id, DisconnectReason::IncorrectSoftwareVersion);
            return;
        }

        // sessions.len() == 1 means this is the only connected client and will become
        // the controller (mirrors the logic in on_authenticate). Record their engine
        // version and mods as the reference for all subsequent clients.
        // If the previous controller disconnected in pregame, sessions is empty until
        // the new client connects, so len() == 1 again -- they become the new controller.
        if self.sessions.len() == 1 {
            self.controller_handshake =
                Some((message.engine_version.clone(), message.enabled_mods.clone()));
        } else if let Some((ref engine_version, ref mods)) = self.controller_handshake {
            if !handshake_matches_controller(&message, engine_version, mods) {
                self.disconnect_session(session_id, DisconnectReason::IncorrectSoftwareVersion);
                return;
            }
        }

        // This is unique enough. We will not check uniqueness.
        let guid = Guid::new();

        self.sessions[session_id].set_guid(guid.clone());

        let mut flags: u32 = 0;
        if self.lobby_auth {
            flags |= 0x1; // PS_NETWORK_FLAG_REQUIRE_LOBBYAUTH
        }

        let response = NetMessage::ServerHandshakeResponse(ServerHandshakeResponse {
            use_protocol_version: PS_PROTOCOL_VERSION,
            flags,
            guid,
        });

        self.send(OutboundNetworkMessage::Message {
            peer: self.sessions[session_id].peer,
            data: response.to_bytes(),
        });

        // Override state to LobbyAuthenticate so the client must complete lobby auth
        // before sending the Authenticate message.
        if self.lobby_auth {
            self.sessions[session_id].state = ServerSessionState::LobbyAuthenticate;
        }
    }
    fn on_authenticate(&mut self, session_id: usize, message: crate::messages::Authenticate) {
        log::info!("Client {} authenticated", self.sessions[session_id].peer.0);

        if self.state == ServerState::Loading {
            self.disconnect_session(session_id, DisconnectReason::ServerLoading);
            return;
        }

        // Lobby auth: verify the claimed username matches what the lobby told us
        if self.lobby_auth {
            let claimed = strip_lobby_suffix(&message.name);
            let session_name = self.sessions[session_id].get_user_name();
            if claimed.to_lowercase() != session_name.to_lowercase() {
                log::error!(
                    "Lobby auth: '{}' tried joining as '{}' (expected '{}')",
                    session_name,
                    claimed,
                    session_name
                );
                self.disconnect_session(session_id, DisconnectReason::LobbyAuthFailed);
                return;
            }
        }

        // Check password (salt = player name, matching C++ CNetClient::SetGamePassword)
        if !self.password.is_empty() {
            let expected = crate::utils::hash_cryptographically(&self.password, &message.name);
            if expected != message.password {
                log::error!("Password check failed for '{}'", message.name);
                self.disconnect_session(session_id, DisconnectReason::ServerRefused);
                return;
            }
        }

        let name = Server::sanitise_player_name(&message.name);
        // With lobby auth, names are already unique (lobby-verified). Otherwise deduplicate.
        let name = if self.lobby_auth {
            name
        } else {
            self.deduplicate_player_name(&name)
        };

        if self.banned_players.contains(&name) {
            self.disconnect_session(session_id, DisconnectReason::Banned);
            return;
        }

        let mut server_full = false;
        let mut is_rejoining = false;

        if self.state == ServerState::Pregame {
            server_full = self.sessions.len() >= MAX_CLIENTS;
        } else if self.state == ServerState::InGame {
            // Check whether this is a previously-connected player rejoining.
            // Match by username (same as C++: clients get a new GUID on reconnect).
            for assignment in self.player_assignments.values() {
                if !assignment.enabled && assignment.name == name {
                    is_rejoining = true;
                    break;
                }
            }
            if !is_rejoining {
                log::info!("Allowing \"{}\" to join mid-game as observer", name);
                is_rejoining = true;
            }
        } else {
            // Loading or PostGame: can't join
            self.disconnect_session(session_id, DisconnectReason::ServerAlreadyInGame);
            return;
        }

        if server_full {
            self.disconnect_session(session_id, DisconnectReason::ServerFull);
            return;
        }
        self.next_host_id += 1;
        let new_host_id = self.next_host_id;

        // Track authenticated player names (deduplicate for rejoiners)
        if !self.authenticated_players.contains(&name) {
            self.authenticated_players.push(name.clone());
        }

        self.sessions[session_id].set_user_name(name);
        self.sessions[session_id].set_host_id(new_host_id);

        let is_controller = self.sessions.len() == 1;

        if is_controller {
            self.controller_guid = Some(self.sessions[session_id].get_guid())
        }

        let code = if is_rejoining {
            AuthenticateResultCode::ARC_OK_REJOINING
        } else {
            AuthenticateResultCode::ARC_OK
        };

        let auth_result = NetMessage::AuthenticateResult(AuthenticateResult {
            code,
            host_id: new_host_id as u16,
            is_controller,
            message: "Logged in".to_string(),
        });

        self.send(OutboundNetworkMessage::Message {
            peer: self.sessions[session_id].peer,
            data: auth_result.to_bytes(),
        });

        self.on_user_join(session_id);

        if is_rejoining {
            // Find an existing ingame client to request the game state from.
            // (Prefer the first ingame session, matching C++'s assumption that session 0 is
            // typically the local/most-efficient source.)
            let source_peer = self
                .sessions
                .iter()
                .find(|s| s.state == ServerSessionState::Ingame)
                .map(|s| s.peer);

            if let Some(source_peer) = source_peer {
                let request_msg = self
                    .file_transfer_manager
                    .start_request(source_peer, new_host_id);
                self.send(request_msg);
                log::info!(
                    "Requested join-sync state from peer {:?} for rejoining host_id={}",
                    source_peer,
                    new_host_id
                );
            } else {
                log::warn!("No ingame client available to provide join-sync state");
            }

            // Transition to JoinSyncing; the client will receive JoinSyncStart once we
            // have the state, then load it and send LoadedGame.
            self.sessions[session_id].state = ServerSessionState::JoinSyncing;
        }
    }
    fn on_disconnect(&mut self, session_id: usize) {
        log::warn!("Client {} disconnected", self.sessions[session_id].peer.0);
        crate::metrics::CONNECTED_CLIENTS
            .with_label_values(&[&self.port_label])
            .dec();
        crate::metrics::CLIENT_DISCONNECTS_TOTAL
            .with_label_values(&[&self.port_label])
            .inc();

        // Check for unresigned disconnect from a rated game
        if self.state == ServerState::InGame {
            let guid = self.sessions[session_id].get_guid();
            if let Some(pa) = self.player_assignments.get(&guid) {
                let player_id = pa.player_id;
                let player_name = pa.name.clone();
                if player_id != -1 && !self.resigned_players.contains(&player_id) {
                    if self.is_rated {
                        let minutes = self
                            .game_start_time
                            .map(|t| (Utc::now() - t).num_minutes())
                            .unwrap_or(0);
                        let body = format!(
                            "{} disconnected from a rated game without resigning. Game lasted {} minutes.",
                            player_name, minutes
                        );
                        log::warn!("{}", body);
                        self.send_lobby_muc_message(body);
                    }
                }
            }
        }

        self.on_user_leave(session_id);
        self.sessions.remove(session_id);
        self.update_player_metrics();
    }
    fn on_chat(&mut self, session_id: usize, message: crate::messages::Chat) {
        let data = NetMessage::Chat(Chat {
            sender_guid: self.sessions[session_id].get_guid(),
            message: message.message,
            receivers: vec![],
        });

        if message.receivers.is_empty() {
            self.multicast(
                data,
                vec![ServerSessionState::Pregame, ServerSessionState::Ingame],
            );
        } else {
            let mut receiving_sessions = vec![];
            for (session_id, session) in self.sessions.iter().enumerate() {
                if message.receivers.contains(&session.get_guid()) {
                    receiving_sessions.push(session_id);
                }
            }

            receiving_sessions.iter().for_each(|session_id| {
                self.send(OutboundNetworkMessage::Message {
                    peer: self.sessions[*session_id].peer,
                    data: data.to_bytes(),
                })
            });
        }
    }
    fn on_ready(&mut self, session_id: usize, message: crate::messages::Ready) {
        if self.state == ServerState::Loading {
            return;
        }

        let out_message = NetMessage::Ready(Ready {
            guid: self.sessions[session_id].get_guid(),
            status: message.status,
        });

        self.multicast(out_message, vec![ServerSessionState::Pregame]);

        self.player_assignments
            .get_mut(&self.sessions[session_id].get_guid())
            .unwrap()
            .status = message.status;
        self.update_player_metrics();
    }
    fn on_clear_all_ready(&mut self, session_id: usize) {
        if self.is_controller(session_id) {
            for (guid, pa) in self.player_assignments.iter_mut() {
                if pa.status != 2 {
                    pa.status = 0;
                }

                // Controller is always ready!
                if self.controller_guid.as_ref() == Some(guid) {
                    pa.status = 1;
                }
            }
            self.send_player_assignments();
            self.update_player_metrics();
        }
    }
    fn on_game_setup(&mut self, session_id: usize, message: crate::messages::GameSetup) {
        if self.is_controller(session_id) {
            self.multicast(
                NetMessage::GameSetup(message),
                vec![ServerSessionState::Pregame],
            );
        }
    }
    fn on_assign_player(&mut self, session_id: usize, message: crate::messages::AssignPlayer) {
        if self.is_controller(session_id) {
            // Remove anyone who's already assigned to this player
            for pa in self.player_assignments.values_mut() {
                if pa.player_id == message.player_id as i32 {
                    pa.player_id = -1;
                }
            }
            // Update this host's assignment if it exists
            if let Some(pa) = self.player_assignments.get_mut(&message.guid) {
                pa.player_id = message.player_id as i32;
            }

            self.send_player_assignments();
        }
    }
    fn on_kick_player(&mut self, session_id: usize, message: crate::messages::Kicked) {
        if !self.is_controller(session_id) {
            return;
        }

        // Find the target session by name.
        let Some(target_idx) = self
            .sessions
            .iter()
            .position(|s| s.get_user_name() == message.name)
        else {
            log::warn!("Kick: player '{}' not found", message.name);
            return;
        };

        // Cannot kick the controller.
        if Some(&self.sessions[target_idx].get_guid()) == self.controller_guid.as_ref() {
            return;
        }

        let ban = message.ban;
        let name = &message.name;
        let peer = self.sessions[target_idx].peer;

        // If banning, record the IP and player name so future connections are rejected.
        if ban {
            let ip = self.sessions[target_idx].address;
            if !self.banned_ips.contains(&ip) {
                self.banned_ips.push(ip);
            }

            // Strip lobby suffix (e.g. " (1234)") when using lobby auth.
            let ban_name = if self.lobby_auth {
                strip_lobby_suffix(name).to_string()
            } else {
                name.to_string()
            };
            if !self.banned_players.contains(&ban_name) {
                self.banned_players.push(ban_name);
            }
        }

        self.multicast(
            NetMessage::Kicked(message),
            vec![
                ServerSessionState::Pregame,
                ServerSessionState::JoinSyncing,
                ServerSessionState::Ingame,
            ],
        );

        self.send(OutboundNetworkMessage::Disconnect {
            peer,
            reason: if ban {
                DisconnectReason::Banned as u32
            } else {
                DisconnectReason::Kicked as u32
            },
        });
    }
    fn on_game_start(&mut self, session_id: usize, message: crate::messages::GameStart) {
        if self.is_controller(session_id) {
            self.pre_start_game(&message.init_attributes);

            self.multicast(
                NetMessage::GameStart(message),
                vec![ServerSessionState::Pregame],
            );
        }
    }
    fn on_saved_game_start(
        &mut self,
        session_id: usize,
        _message: crate::messages::GameSavedStart,
    ) {
        log::info!("Saved Game start from {}", self.sessions[session_id].peer.0);
    }
    fn on_rejoined(&mut self, session_id: usize, _message: crate::messages::Rejoined) {
        // Overwrite the GUID with the server-authoritative value
        let guid = self.sessions[session_id].get_guid();
        let peer = self.sessions[session_id].peer;
        log::info!("Client {:?} (guid={:?}) rejoined", peer, guid);

        self.multicast(
            NetMessage::Rejoined(Rejoined { guid }),
            vec![ServerSessionState::Ingame],
        );

        for paused_guid in self.pausing_players.clone() {
            self.send(OutboundNetworkMessage::Message {
                peer,
                data: NetMessage::ClientPaused(ClientPaused {
                    guid: Guid(paused_guid),
                    pause: true,
                })
                .to_bytes(),
            });
        }
    }
    fn on_client_paused(&mut self, session_id: usize, mut message: crate::messages::ClientPaused) {
        let guid = self.sessions[session_id].get_guid();
        // Overwrite the GUID with the server-authoritative value to prevent spoofing.
        message.guid = guid.clone();

        if message.pause {
            if !self.pausing_players.contains(&guid.to_string()) {
                self.pausing_players.push(guid.to_string());
            }
        } else {
            self.pausing_players.retain(|g| *g != guid.to_string());
        }

        crate::metrics::PLAYERS_PAUSED
            .with_label_values(&[&self.port_label])
            .set(self.pausing_players.len() as f64);
        self.multicast(
            NetMessage::ClientPaused(message),
            vec![ServerSessionState::Ingame],
        );
    }
    fn on_flare(&mut self, session_id: usize, mut message: crate::messages::Flare) {
        log::debug!("Flare from {}", self.sessions[session_id].peer.0);
        message.guid = self.sessions[session_id].get_guid();
        self.multicast(NetMessage::Flare(message), vec![ServerSessionState::Ingame]);
    }
    fn on_sync_check(&mut self, session_id: usize, message: crate::messages::SyncCheck) {
        let host_id = self.sessions[session_id].get_host_id();
        let user_name = self.sessions[session_id].get_user_name();
        let actions = self.server_turn_manager.notify_finished_client_update(
            host_id,
            &user_name,
            message.turn,
            message.hash,
        );
        self.process_turn_manager_actions(actions);
    }
    fn on_end_command_batch(
        &mut self,
        session_id: usize,
        message: crate::messages::EndCommandBatch,
    ) {
        let host_id = self.sessions[session_id].get_host_id();
        let user_name = self.sessions[session_id].get_user_name();
        let actions = self.server_turn_manager.notify_finished_client_commands(
            host_id,
            &user_name,
            message.turn,
        );
        self.process_turn_manager_actions(actions);
    }
    fn on_loaded_game(&mut self, session_id: usize, _message: crate::messages::LoadedGame) {
        // We're in the loading state, so wait until every client has loaded
        // before starting the game
        if self.state != ServerState::Loading {
            return;
        }
        if self.check_game_load_status(session_id) {
            return;
        }

        // We always send all GUIDs of clients in the loading state
        // so that we don't have to bother about switching GUI pages
        let mut loading_guids = Vec::new();
        for session in &self.sessions {
            if session.state != ServerSessionState::Ingame
                && session.get_guid() != self.sessions[session_id].get_guid()
            {
                loading_guids.push(session.get_guid());
            }
        }

        // If no other player is loading the server can clear the savestate
        if loading_guids.is_empty() {
            self.saved_state = None;
        }

        let message = NetMessage::ClientsLoading(ClientsLoading {
            clients: loading_guids,
        });

        self.send(OutboundNetworkMessage::Message {
            peer: self.sessions[session_id].peer,
            data: message.to_bytes(),
        });

        self.multicast(message, vec![ServerSessionState::Ingame]);
    }

    fn check_game_load_status(&mut self, changed_session_id: usize) -> bool {
        for (session_id, _session) in self.sessions.iter().enumerate() {
            if session_id != changed_session_id
                && self.sessions[session_id].state != ServerSessionState::Ingame
            {
                return false; // not everyone loaded yet
            }
        }

        let message = NetMessage::LoadedGame(LoadedGame { current_turn: 0 });
        self.multicast(
            message,
            vec![ServerSessionState::Pregame, ServerSessionState::Ingame],
        );

        for session_id in 0..self.sessions.len() {
            let state = self.sessions[session_id].state;
            if state == ServerSessionState::Pregame || state == ServerSessionState::Ingame {
                let host_id = self.sessions[session_id].get_host_id();
                let guid = self.sessions[session_id].get_guid();
                self.server_turn_manager
                    .initialise_client(host_id, 0, self.is_observer(&guid));
            }
        }

        self.state = ServerState::InGame;
        self.game_start_time = Some(Utc::now());
        crate::metrics::GAME_STATE
            .with_label_values(&[&self.port_label])
            .set(2.0); // InGame
        true
    }

    fn on_join_syncing_loaded_game(
        &mut self,
        session_id: usize,
        message: crate::messages::LoadedGame,
    ) {
        // The rejoining client has loaded the saved state at `message.current_turn`.
        // Replay all simulation commands and turn-end markers received since then so
        // the client can catch up to the current ready turn.
        let turn = message.current_turn;
        let ready_turn = self.server_turn_manager.get_ready_turn();
        let host_id = self.sessions[session_id].get_host_id();
        let peer = self.sessions[session_id].peer;

        log::info!(
            "Client host_id={} finished join syncing at turn={}, ready_turn={}",
            host_id,
            turn,
            ready_turn
        );

        let max_turn = std::cmp::max(ready_turn as usize + 1, self.saved_commands.len());

        for i in (turn as usize + 1)..max_turn {
            if i < self.saved_commands.len() {
                for cmd in self.saved_commands[i].clone() {
                    self.send(OutboundNetworkMessage::Message {
                        peer,
                        data: NetMessage::Simulation(cmd).to_bytes(),
                    });
                }
            }
            if (i as u32) <= ready_turn {
                let turn_length = self.server_turn_manager.get_saved_turn_length(i as u32) as u16;
                self.send(OutboundNetworkMessage::Message {
                    peer,
                    data: NetMessage::EndCommandBatch(EndCommandBatch {
                        turn: i as u32,
                        turn_length,
                    })
                    .to_bytes(),
                });
            }
        }

        let guid = self.sessions[session_id].get_guid();
        self.server_turn_manager
            .initialise_client(host_id, ready_turn, self.is_observer(&guid));

        self.send(OutboundNetworkMessage::Message {
            peer,
            data: NetMessage::LoadedGame(LoadedGame {
                current_turn: ready_turn,
            })
            .to_bytes(),
        });
    }
    fn on_simulation_command(&mut self, session_id: usize, message: Simulation) {
        // Validate: the sender's assigned player must match message.player
        let session_guid = self.sessions[session_id].get_guid();
        if !self.cheats_enabled {
            match self.player_assignments.get(&session_guid) {
                Some(assignment) if assignment.player_id == message.player => {}
                _ => {
                    log::warn!(
                        "Disconnecting session {} (GUID {:?}): simulation command player mismatch",
                        session_id,
                        session_guid
                    );
                    self.disconnect_session(session_id, DisconnectReason::ServerRefused);
                    return;
                }
            }
        }

        let cmd_type = Simulation::extract_command_type(&message.data)
            .unwrap_or_else(|| "unknown".to_string());

        if cmd_type == "resign" {
            self.resigned_players.insert(message.player);
        }

        self.multicast(
            NetMessage::Simulation(message.clone()),
            vec![ServerSessionState::Ingame],
        );

        let turn = message.turn as usize;
        if self.saved_commands.len() < turn + 1 {
            self.saved_commands.resize_with(turn + 1, Vec::new);
        }
        self.saved_commands[turn].push(message);
    }
}

#[derive(Debug)]
struct ServerPlayerAssignment {
    /// Whether the player is currently connected and active.
    /// (We retain information on disconnected players to support rejoining,
    /// but don't transmit these to other clients.)
    enabled: bool,

    /// Player name
    name: String,

    /// The player that the given host controls, or -1 if none (observer)
    player_id: i32,

    /// Status - Ready or not: 0 for not ready, 1 for ready, 2 to stay ready
    status: u8,
}

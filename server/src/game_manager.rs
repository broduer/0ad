use std::{
    collections::HashMap,
    net::{IpAddr, SocketAddr},
    sync::mpsc,
    thread::JoinHandle,
};

use crate::{
    enet_task::run_enet_host,
    net_server::Server,
    network_message::{InboundNetworkMessage, OutboundNetworkMessage},
};

pub type GameId = u32;

pub struct GameConfig {
    pub password: String,
    pub is_saved_game: bool,
    pub use_lobby_auth: Option<bool>,
    /// Lobby channel for receiving lobby auth events (XMPP -> game thread).
    pub lobby_rx: Option<tokio::sync::mpsc::Receiver<crate::lobby::types::LobbyToGameEvent>>,
    /// Lobby channel for sending game lifecycle events (game thread -> XMPP).
    pub lobby_tx: Option<tokio::sync::mpsc::Sender<crate::lobby::types::GameToLobbyEvent>>,
    /// Server name for lobby registration.
    pub server_name: String,
}

struct GameHandle {
    port: u16,
    /// Dropping this sender triggers the shutdown cascade:
    /// ENet thread sees Disconnected, breaks, drops event_tx ->
    /// Server sees Disconnected on event_rx, returns.
    _shutdown_tx: mpsc::Sender<()>,
    server_thread: Option<JoinHandle<()>>,
    enet_thread: Option<JoinHandle<()>>,
    /// Optional lobby account ID for releasing on game destruction.
    lobby_account_id: Option<usize>,
}

pub struct GameManager {
    bind_ip: IpAddr,
    next_game_id: GameId,
    games: HashMap<GameId, GameHandle>,
    used_ports: Vec<u16>,
    port_range: (u16, u16),
}

impl GameManager {
    pub fn new(bind_ip: IpAddr) -> Self {
        Self {
            bind_ip,
            next_game_id: 0,
            games: HashMap::new(),
            used_ports: Vec::new(),
            port_range: (20595, 20695),
        }
    }

    fn allocate_port(&mut self) -> Option<u16> {
        let (start, end) = self.port_range;
        (start..=end).find(|p| !self.used_ports.contains(p))
    }

    pub fn create_game(
        &mut self,
        port: Option<u16>,
        config: GameConfig,
        lobby_account_id: Option<usize>,
    ) -> Result<(GameId, u16), String> {
        let port = match port {
            Some(p) => {
                if self.used_ports.contains(&p) {
                    return Err(format!("Port {} is already in use", p));
                }
                p
            }
            None => self
                .allocate_port()
                .ok_or_else(|| "No free ports in range".to_string())?,
        };

        let bind_addr = SocketAddr::new(self.bind_ip, port);

        let (event_tx, event_rx) = mpsc::channel::<InboundNetworkMessage>();
        let (send_tx, send_rx) = mpsc::channel::<OutboundNetworkMessage>();
        let (shutdown_tx, shutdown_rx) = mpsc::channel::<()>();

        let enet_thread = std::thread::spawn(move || {
            run_enet_host(bind_addr, event_tx, send_rx, shutdown_rx);
        });

        let public_ip = self.bind_ip;
        let password = config.password.clone();
        let server_name = config.server_name.clone();
        let lobby_rx = config.lobby_rx;
        let lobby_tx = config.lobby_tx;
        let server_thread = std::thread::spawn(move || {
            let mut server = Server::new(
                send_tx,
                config.is_saved_game,
                config.use_lobby_auth,
                port,
                public_ip,
                password,
                server_name,
                lobby_rx,
                lobby_tx,
            );
            server.start(event_rx);
        });

        let game_id = self.next_game_id;
        self.next_game_id += 1;
        self.used_ports.push(port);

        self.games.insert(
            game_id,
            GameHandle {
                port,
                _shutdown_tx: shutdown_tx,
                server_thread: Some(server_thread),
                enet_thread: Some(enet_thread),
                lobby_account_id,
            },
        );

        crate::metrics::GAMES_CREATED_TOTAL.inc();
        crate::metrics::ACTIVE_GAMES.set(self.games.len() as f64);

        log::info!("Game {} created on port {}", game_id, port);
        Ok((game_id, port))
    }

    /// Destroy a game and return the lobby account ID (if any) for releasing.
    pub fn destroy_game(&mut self, game_id: GameId) -> Option<usize> {
        let Some(mut handle) = self.games.remove(&game_id) else {
            log::warn!("destroy_game: unknown game_id {}", game_id);
            return None;
        };

        let port = handle.port;
        let lobby_account_id = handle.lobby_account_id;

        // Drop shutdown_tx -- this triggers the cascade:
        // ENet thread sees Disconnected on shutdown_rx, breaks, drops event_tx ->
        // Server sees Disconnected on event_rx, returns.
        drop(handle._shutdown_tx);

        if let Some(t) = handle.enet_thread.take() {
            let _ = t.join();
        }
        if let Some(t) = handle.server_thread.take() {
            let _ = t.join();
        }

        self.used_ports.retain(|&p| p != port);
        crate::metrics::ACTIVE_GAMES.set(self.games.len() as f64);
        log::info!("Game {} on port {} destroyed", game_id, port);
        lobby_account_id
    }

    /// Destroy all running games.
    pub fn destroy_all_games(&mut self) {
        let ids: Vec<GameId> = self.games.keys().copied().collect();
        for id in ids {
            self.destroy_game(id);
        }
    }

    /// Find the game associated with a lobby account ID.
    pub fn find_game_by_account(&self, account_id: usize) -> Option<GameId> {
        self.games
            .iter()
            .find(|(_, h)| h.lobby_account_id == Some(account_id))
            .map(|(&id, _)| id)
    }
}

impl Drop for GameManager {
    fn drop(&mut self) {
        let ids: Vec<GameId> = self.games.keys().copied().collect();
        for id in ids {
            self.destroy_game(id);
        }
    }
}

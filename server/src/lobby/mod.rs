pub mod stanza_builders;
pub mod types;
mod xmpp_account;

use tokio::sync::mpsc;
use tokio::task::JoinHandle;

use types::*;
use xmpp_account::run_xmpp_account;

struct AccountSlot {
    control_tx: mpsc::Sender<AccountControl>,
    handle: Option<JoinHandle<()>>,
    in_use: bool,
}

pub struct LobbyManager {
    config: LobbyConfig,
    accounts: Vec<AccountSlot>,
}

impl LobbyManager {
    pub fn new(config: LobbyConfig) -> Self {
        Self {
            config,
            accounts: Vec::new(),
        }
    }

    /// Spawn all XMPP account tasks on the tokio runtime.
    /// Returns a receiver for events from XMPP tasks (e.g. HostRequested).
    /// Must be called from within a tokio context.
    pub fn start(&mut self) -> mpsc::Receiver<XmppToMainEvent> {
        // Install rustls crypto provider (required by tokio-xmpp)
        let _ = tokio_xmpp::rustls::crypto::aws_lc_rs::default_provider().install_default();

        let (main_tx, main_rx) = mpsc::channel::<XmppToMainEvent>(32);

        for (idx, creds) in self.config.accounts.iter().enumerate() {
            let (control_tx, control_rx) = mpsc::channel::<AccountControl>(16);

            let creds = creds.clone();
            let muc_room = self.config.muc_room.clone();
            let bot_jid = self.config.bot_jid.clone();
            let public_ip = self.config.public_ip.clone();
            let engine_version = self.config.engine_version.clone();
            let task_main_tx = main_tx.clone();

            let handle = tokio::spawn(async move {
                run_xmpp_account(
                    creds,
                    muc_room,
                    bot_jid,
                    public_ip,
                    engine_version,
                    control_rx,
                    idx,
                    task_main_tx,
                )
                .await;
            });

            log::info!(
                "Spawned XMPP account task {} ({})",
                idx,
                self.config.accounts[idx].jid
            );

            self.accounts.push(AccountSlot {
                control_tx,
                handle: Some(handle),
                in_use: false,
            });
        }

        main_rx
    }

    /// Reserve a free XMPP account slot and create channels for a game.
    /// Does NOT notify the XMPP task yet -- call `activate_game` after the
    /// game is created and the actual port is known.
    pub fn prepare_game(&mut self) -> Option<LobbyGameHandle> {
        let slot_idx = self.accounts.iter().position(|s| !s.in_use)?;

        let (lobby_tx, lobby_rx) = mpsc::channel::<LobbyToGameEvent>(32);
        let (game_tx, game_rx) = mpsc::channel::<GameToLobbyEvent>(32);

        self.accounts[slot_idx].in_use = true;

        Some(LobbyGameHandle {
            lobby_rx,
            game_rx: Some(game_rx),
            lobby_tx: Some(lobby_tx),
            game_tx,
            account_id: slot_idx,
        })
    }

    /// Notify the XMPP task that a game is ready on the given port.
    /// Must be called after `prepare_game` and after the game has been created.
    pub fn activate_game(
        &self,
        account_id: usize,
        lobby_tx: mpsc::Sender<LobbyToGameEvent>,
        game_rx: mpsc::Receiver<GameToLobbyEvent>,
        password: String,
        port: u16,
    ) {
        if let Some(slot) = self.accounts.get(account_id) {
            let _ = slot.control_tx.try_send(AccountControl::AssignGame {
                lobby_tx,
                game_rx,
                password,
                port,
            });
        }
    }

    /// Release an XMPP account after a game ends.
    pub fn release_game(&mut self, account_id: usize) {
        if let Some(slot) = self.accounts.get_mut(account_id) {
            let _ = slot.control_tx.try_send(AccountControl::ReleaseGame);
            slot.in_use = false;
        }
    }

    /// Send a groupchat message to the MUC room through a specific account.
    pub fn send_muc_message(&self, account_id: usize, body: String) {
        if let Some(slot) = self.accounts.get(account_id) {
            let _ = slot
                .control_tx
                .try_send(AccountControl::SendMucMessage { body });
        }
    }

    /// Check if a specific account slot is currently in use.
    pub fn is_account_in_use(&self, account_id: usize) -> bool {
        self.accounts.get(account_id).map_or(false, |s| s.in_use)
    }

    /// Send a shutdown signal to all XMPP account tasks and wait for them to finish.
    pub async fn shutdown(&mut self) {
        for (idx, slot) in self.accounts.iter().enumerate() {
            log::info!("Sending shutdown to XMPP account {}", idx);
            let _ = slot.control_tx.try_send(AccountControl::Shutdown);
        }
        for (idx, slot) in self.accounts.iter_mut().enumerate() {
            if let Some(handle) = slot.handle.take() {
                log::info!("Waiting for XMPP account {} to finish...", idx);
                let _ = handle.await;
            }
        }
    }
}

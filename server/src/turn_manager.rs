use std::collections::{BTreeMap, HashMap};

use crate::{server_session::DisconnectReason, utils::hexify};

/// Actions the TurnManager wants the Server to perform.
/// The TurnManager never does network I/O itself; it returns these
/// for the Server to execute after each call.
#[derive(Debug)]
pub enum TurnManagerAction {
    /// Multicast an EndCommandBatch to all clients in the Ingame state.
    BroadcastEndCommandBatch { turn: u32, turn_length: u16 },
    /// Multicast a SyncError to all clients in the Ingame state.
    BroadcastSyncError {
        turn: u32,
        hash_expected: Vec<u8>,
        player_names: Vec<String>,
    },
    /// Disconnect a specific client due to a protocol violation.
    DisconnectClient {
        host_id: u32,
        reason: DisconnectReason,
    },
}

struct Client {
    /// Latest turn for which all commands have been received.
    ready_turn: u32,
    player_name: String,
    /// Last known simulated turn.
    simulated_turn: u32,
    is_observer: bool,
    is_oos: bool,
}

pub struct TurnManager {
    clients_data: HashMap<u32, Client>,
    /// Cached value - is any client OOS? This is reset when the OOS client leaves.
    has_sync_error: bool,
    /// Map of turn -> {Client ID -> state hash}; old indexes <= min(m_ClientsSimulated) are deleted.
    /// BTreeMap ensures ordered iteration for OOS detection.
    client_state_hashes: BTreeMap<u32, HashMap<u32, Vec<u8>>>,
    /// The latest turn for which we have received all commands from all clients
    ready_turn: u32,
    /// Current turn length, milliseconds
    turn_length: u32,
    /// Turn lengths for all previously executed turns
    saved_turn_lengths: Vec<u32>,
}

const COMMAND_DELAY_MP: u32 = 4;
const DEFAULT_TURN_LENGTH: u32 = 200;

impl TurnManager {
    pub fn new() -> Self {
        let mut saved_turn_lengths = Vec::with_capacity(COMMAND_DELAY_MP as usize);

        for _ in 0..COMMAND_DELAY_MP {
            saved_turn_lengths.push(DEFAULT_TURN_LENGTH);
        }

        Self {
            clients_data: HashMap::new(),
            has_sync_error: false,
            client_state_hashes: BTreeMap::new(),
            ready_turn: COMMAND_DELAY_MP - 1,
            turn_length: DEFAULT_TURN_LENGTH,
            saved_turn_lengths,
        }
    }

    pub fn notify_finished_client_commands(
        &mut self,
        host_id: u32,
        user_name: &str,
        turn: u32,
    ) -> Vec<TurnManagerAction> {
        let mut actions = Vec::new();

        log::info!(
            target: "NETSERVERTURN_LOG",
            "notify_finished_client_commands(client={}, turn={})\n",
            host_id,
            turn
        );

        if let Some(client) = self.clients_data.get_mut(&host_id) {
            // Clients must advance one turn at a time
            if turn != client.ready_turn + 1 {
                log::error!(
                    "NotifyFinishedClientCommands: Client {} ({}) is ready for turn {}, but expected {}",
                    host_id,
                    user_name,
                    turn,
                    client.ready_turn + 1
                );
                actions.push(TurnManagerAction::DisconnectClient {
                    host_id,
                    reason: DisconnectReason::IncorrectReadyTurnCommands,
                });
                return actions;
            }

            client.ready_turn = turn;

            // Check whether this was the final client to become ready
            actions.extend(self.check_clients_ready());
        } else {
            log::error!(
                "Must be a client we've already heard of. host_id: {}",
                host_id
            );
        }

        actions
    }

    pub fn notify_finished_client_update(
        &mut self,
        host_id: u32,
        user_name: &str,
        turn: u32,
        hash: Vec<u8>,
    ) -> Vec<TurnManagerAction> {
        let mut actions = Vec::new();

        // Clients must advance one turn at a time
        let client = self
            .clients_data
            .get_mut(&host_id)
            .expect("client should be present");
        let expected_turn = client.simulated_turn + 1;
        if turn != expected_turn {
            log::error!(
                "notify_finished_client_update: Client {} ({}) is ready for turn {}, but expected {}",
                host_id,
                user_name,
                turn,
                expected_turn
            );
            actions.push(TurnManagerAction::DisconnectClient {
                host_id,
                reason: DisconnectReason::IncorrectReadyTurnSimulated,
            });
            return actions;
        }
        client.simulated_turn = turn;

        // Check for OOS only if in sync
        if self.has_sync_error {
            return actions;
        }

        client.player_name = user_name.to_string();

        self.client_state_hashes
            .entry(turn)
            .or_default()
            .insert(host_id, hash);

        // Find the newest turn which we know all clients have simulated
        let mut newest = u32::MAX;
        for (_id, client) in self.clients_data.iter() {
            if client.simulated_turn < newest {
                newest = client.simulated_turn;
            }
        }

        // For every set of state hashes that all clients have simulated, check for OOS
        for (turn, hashes) in self.client_state_hashes.iter() {
            if *turn > newest {
                break;
            }

            // Assume the host is correct (maybe we should choose the most common instead to help debugging)
            let expected = hashes
                .iter()
                .next()
                .expect("client_state_hashes should have at least one hash")
                .1;

            // Find all players that are OOS on that turn
            let mut oos_player_names = vec![];
            for (client, hash) in hashes.iter() {
                log::info!(
                    target: "NETSERVERTURN_LOG",
                    "sync check {}: {} = {}\n",
                    turn,
                    client,
                    hexify(hash)
                );
                if hash != expected {
                    self.has_sync_error = true;
                    self.clients_data.get_mut(client).unwrap().is_oos = true;
                    oos_player_names
                        .push(self.clients_data.get(client).unwrap().player_name.clone());
                }
            }

            if self.has_sync_error {
                actions.push(TurnManagerAction::BroadcastSyncError {
                    turn: *turn,
                    hash_expected: expected.clone(),
                    player_names: oos_player_names,
                });
                break;
            }
        }

        // Delete the saved hashes for all turns that we've already verified
        self.client_state_hashes
            .retain(|turn, _hashes| *turn > newest);

        actions
    }

    /// Inform the turn manager of a new client.
    pub fn initialise_client(&mut self, client: u32, turn: u32, observer: bool) {
        log::info!(
            target: "NETSERVERTURN_LOG",
            "initialise_client(client={}, turn={})\n",
            client,
            turn
        );

        if self.clients_data.contains_key(&client) {
            log::error!(target: "ENSURE", "host_id {} is already initialized", client);
        }

        self.clients_data.insert(
            client,
            Client {
                ready_turn: turn + COMMAND_DELAY_MP - 1,
                player_name: "Unknown".to_string(),
                simulated_turn: turn,
                is_observer: observer,
                is_oos: false,
            },
        );
    }

    /// Inform the turn manager that a previously-initialised client has left the game.
    pub fn uninitialise_client(&mut self, client: u32) -> Vec<TurnManagerAction> {
        log::info!(
            target: "NETSERVERTURN_LOG",
            "uninitialise_client(client={})\n",
            client
        );

        if !self.clients_data.contains_key(&client) {
            log::debug!(target: "ENSURE", "host_id {} is not initialized", client);
            return Vec::new();
        }

        let check_oos = self.clients_data[&client].is_oos;

        self.clients_data.remove(&client);

        for (_turn, clients) in self.client_state_hashes.iter_mut() {
            clients.remove(&client);
        }

        // Check whether we're ready for the next turn now that we're not
        // waiting for this client any more
        let actions = self.check_clients_ready();

        // Check whether we're still OOS.
        if check_oos {
            let any_still_oos = self.clients_data.values().any(|data| data.is_oos);
            if !any_still_oos {
                self.has_sync_error = false;
            }
        }

        actions
    }

    /// Returns the latest turn for which all clients are ready;
    /// they will have already been told to execute this turn.
    pub fn get_ready_turn(&self) -> u32 {
        self.ready_turn
    }

    /// Returns the turn length that was used for the given turn.
    /// Requires turn <= GetReadyTurn().
    pub fn get_saved_turn_length(&self, turn: u32) -> u32 {
        *self
            .saved_turn_lengths
            .get(turn as usize)
            .expect("Turn length should be saved at this point")
    }

    fn check_clients_ready(&mut self) -> Vec<TurnManagerAction> {
        let mut actions = Vec::new();
        let max_observer_lag: i32 = 50; // in turns TODO: read from config

        // Clamp to 0-10000 turns, below/above that is no limit.
        let max_observer_lag = if !(0..=10000).contains(&max_observer_lag) {
            -1
        } else {
            max_observer_lag
        };

        // See if all clients are ready for a new turn
        for (_client_index, client) in self.clients_data.iter() {
            // Observers are allowed to lag more than regular clients.
            if client.is_observer
                && (max_observer_lag == -1
                    || client.ready_turn > self.ready_turn - max_observer_lag as u32)
            {
                continue;
            }

            if client.ready_turn <= self.ready_turn {
                return actions; // wasn't ready for self.ready_turn+1
            }
        }

        self.ready_turn += 1;

        log::info!(
            target: "NETSERVERTURN_LOG",
            "check_clients_ready: ready for turn {}\n",
            self.ready_turn
        );

        actions.push(TurnManagerAction::BroadcastEndCommandBatch {
            turn: self.ready_turn,
            turn_length: self.turn_length as u16,
        });

        if self.saved_turn_lengths.len() == self.ready_turn as usize {
            self.saved_turn_lengths.push(self.turn_length);
        } else {
            log::error!(
                target: "ENSURE",
                "self.saved_turn_lengths.len(){} != self.ready_turn {}",
                self.saved_turn_lengths.len(),
                self.ready_turn
            );
        }

        actions
    }
}

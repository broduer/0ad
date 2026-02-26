use once_cell::sync::Lazy;
use prometheus::{
    GaugeVec, IntCounter, IntCounterVec, register_gauge_vec, register_int_counter,
    register_int_counter_vec,
};

pub static ACTIVE_GAMES: Lazy<prometheus::Gauge> = Lazy::new(|| {
    prometheus::register_gauge!("active_games", "Number of currently active games").unwrap()
});

pub static GAMES_CREATED_TOTAL: Lazy<IntCounter> = Lazy::new(|| {
    register_int_counter!(
        "games_created_total",
        "Total number of games created since startup"
    )
    .unwrap()
});

pub static CONNECTED_CLIENTS: Lazy<GaugeVec> = Lazy::new(|| {
    register_gauge_vec!(
        "game_connected_clients",
        "Number of connected clients",
        &["port"]
    )
    .unwrap()
});

pub static GAME_STATE: Lazy<GaugeVec> = Lazy::new(|| {
    register_gauge_vec!(
        "game_state",
        "Current game state (0=Pregame, 1=Loading, 2=InGame, 3=PostGame)",
        &["port"]
    )
    .unwrap()
});

pub static PLAYERS_TOTAL: Lazy<GaugeVec> = Lazy::new(|| {
    register_gauge_vec!(
        "game_players_total",
        "Total active players (excluding observers)",
        &["port"]
    )
    .unwrap()
});

pub static PLAYERS_READY: Lazy<GaugeVec> = Lazy::new(|| {
    register_gauge_vec!(
        "game_players_ready",
        "Number of players with ready status",
        &["port"]
    )
    .unwrap()
});

pub static PLAYERS_PAUSED: Lazy<GaugeVec> = Lazy::new(|| {
    register_gauge_vec!(
        "game_players_paused",
        "Number of currently pausing players",
        &["port"]
    )
    .unwrap()
});

pub static CURRENT_TURN: Lazy<GaugeVec> = Lazy::new(|| {
    register_gauge_vec!("game_current_turn", "Current ready turn number", &["port"]).unwrap()
});

pub static OOS_ERRORS_TOTAL: Lazy<IntCounterVec> = Lazy::new(|| {
    register_int_counter_vec!(
        "game_oos_errors_total",
        "Number of out-of-sync errors detected",
        &["port"]
    )
    .unwrap()
});

pub static CLIENT_DISCONNECTS_TOTAL: Lazy<IntCounterVec> = Lazy::new(|| {
    register_int_counter_vec!(
        "game_client_disconnects_total",
        "Total client disconnections",
        &["port"]
    )
    .unwrap()
});

pub static MESSAGES_RECEIVED_TOTAL: Lazy<IntCounterVec> = Lazy::new(|| {
    register_int_counter_vec!(
        "game_messages_received_total",
        "Total messages received, labeled by type",
        &["port", "msg_type"]
    )
    .unwrap()
});

pub fn encode() -> String {
    use prometheus::{Encoder, TextEncoder};
    let encoder = TextEncoder::new();
    let families = prometheus::gather();
    let mut buf = Vec::new();
    encoder.encode(&families, &mut buf).unwrap();
    String::from_utf8(buf).unwrap()
}

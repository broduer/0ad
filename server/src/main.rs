mod enet_task;
mod file_transfer;
mod game_manager;
mod lobby;
mod messages;
mod metrics;
mod net_server;
mod network_message;
mod server_session;
mod turn_manager;
mod utils;

use std::{
    collections::{HashMap, HashSet},
    net::IpAddr,
    path::PathBuf,
    str::FromStr,
};

use crate::game_manager::{GameConfig, GameManager};
use crate::lobby::types::{LobbyConfig, XmppToMainEvent};

const DEFAULT_HOST: &str = "0.0.0.0";
const DEFAULT_PORT: u16 = 20595; // 0x5073 -- matches PS_DEFAULT_PORT in NetMessages.h
const DEFAULT_METRICS_PORT: u16 = 9091;

fn parse_args() -> (IpAddr, u16, u16, Option<PathBuf>) {
    let mut host = IpAddr::from_str(DEFAULT_HOST).unwrap();
    let mut port = DEFAULT_PORT;
    let mut metrics_port = DEFAULT_METRICS_PORT;
    let mut lobby_config_path: Option<PathBuf> = None;
    let args: Vec<String> = std::env::args().collect();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "--host" => {
                i += 1;
                host = IpAddr::from_str(args.get(i).unwrap_or_else(|| {
                    eprintln!("Error: --host requires an IP address");
                    std::process::exit(1);
                }))
                .unwrap_or_else(|e| {
                    eprintln!("Error: invalid host address '{}': {}", args[i], e);
                    std::process::exit(1);
                });
            }
            "--port" => {
                i += 1;
                port = args
                    .get(i)
                    .unwrap_or_else(|| {
                        eprintln!("Error: --port requires a port number");
                        std::process::exit(1);
                    })
                    .parse()
                    .unwrap_or_else(|e| {
                        eprintln!("Error: invalid port '{}': {}", args[i], e);
                        std::process::exit(1);
                    });
            }
            "--metrics-port" => {
                i += 1;
                metrics_port = args
                    .get(i)
                    .unwrap_or_else(|| {
                        eprintln!("Error: --metrics-port requires a port number");
                        std::process::exit(1);
                    })
                    .parse()
                    .unwrap_or_else(|e| {
                        eprintln!("Error: invalid metrics port '{}': {}", args[i], e);
                        std::process::exit(1);
                    });
            }
            "--lobby-config" => {
                i += 1;
                let path_str = args.get(i).unwrap_or_else(|| {
                    eprintln!("Error: --lobby-config requires a file path");
                    std::process::exit(1);
                });
                lobby_config_path = Some(PathBuf::from(path_str));
            }
            "--help" | "-h" => {
                println!("Usage: {} [OPTIONS]", args[0]);
                println!();
                println!("Options:");
                println!(
                    "  --host <IP>            Bind address (default: {})",
                    DEFAULT_HOST
                );
                println!(
                    "  --port <PORT>          Listen port  (default: {})",
                    DEFAULT_PORT
                );
                println!(
                    "  --metrics-port <PORT>  Prometheus metrics port (default: {})",
                    DEFAULT_METRICS_PORT
                );
                println!("  --lobby-config <PATH>  Path to lobby.json config for XMPP integration");
                println!("  --help, -h             Show this help");
                std::process::exit(0);
            }
            other => {
                eprintln!("Error: unknown argument '{}'", other);
                eprintln!("Try '--help' for usage");
                std::process::exit(1);
            }
        }
        i += 1;
    }
    (host, port, metrics_port, lobby_config_path)
}

#[rocket::get("/metrics")]
fn metrics_route() -> (rocket::http::ContentType, String) {
    (
        rocket::http::ContentType::new("text", "plain"),
        crate::metrics::encode(),
    )
}

#[rocket::main]
async fn main() {
    env_logger::Builder::new()
        .filter(None, log::LevelFilter::Debug)
        .parse_default_env()
        .init();

    let (host, port, metrics_port, lobby_config_path) = parse_args();

    // Optionally initialize the lobby (XMPP) manager
    let mut lobby_server_name: Option<String> = None;
    let mut lobby_manager = lobby_config_path.map(|path| {
        let config_data = std::fs::read_to_string(&path).unwrap_or_else(|e| {
            eprintln!(
                "Error: failed to read lobby config '{}': {}",
                path.display(),
                e
            );
            std::process::exit(1);
        });
        let config: LobbyConfig = serde_json::from_str(&config_data).unwrap_or_else(|e| {
            eprintln!("Error: failed to parse lobby config: {}", e);
            std::process::exit(1);
        });
        log::info!(
            "Lobby config loaded: {} accounts, MUC room: {}",
            config.accounts.len(),
            config.muc_room
        );
        lobby_server_name = config.server_name.clone();
        lobby::LobbyManager::new(config)
    });

    let server_name = lobby_server_name.unwrap_or_else(|| "Dedicated Server".to_string());

    // Spawn Rocket metrics server in the background
    let figment = rocket::Config::figment()
        .merge(("port", metrics_port))
        .merge(("address", "0.0.0.0"))
        .merge(("log_level", "off"));

    tokio::spawn(async move {
        let _ = rocket::custom(figment)
            .mount("/", rocket::routes![metrics_route])
            .launch()
            .await;
    });

    log::info!(
        "Prometheus metrics available at http://0.0.0.0:{}/metrics",
        metrics_port
    );

    let mut manager = GameManager::new(host);

    if let Some(ref mut lobby_mgr) = lobby_manager {
        // Start XMPP accounts and get the event receiver
        let mut main_rx = lobby_mgr.start();

        log::info!("Lobby mode: waiting for 'hostme' command in MUC chat to create a game");

        // Event loop: create games on demand when "hostme" is received
        // Track which senders have a pending/active game to deduplicate
        // (all XMPP accounts receive the same MUC message).
        let mut active_senders: HashSet<String> = HashSet::new();
        // Map account_id -> sender name so we can clean up on GameEnded.
        let mut account_sender: HashMap<usize, String> = HashMap::new();

        loop {
            let event = tokio::select! {
                event = main_rx.recv() => match event {
                    Some(e) => e,
                    None => break,
                },
                _ = tokio::signal::ctrl_c() => {
                    log::info!("Ctrl+C received, shutting down...");
                    break;
                }
            };
            match event {
                XmppToMainEvent::HostRequested { account_id, sender } => {
                    // Deduplicate: only process the first event per sender
                    if active_senders.contains(&sender) {
                        log::debug!(
                            "Ignoring duplicate 'hostme' from '{}' (account {})",
                            sender,
                            account_id
                        );
                        continue;
                    }

                    let game_name = format!("Server for {}", sender);

                    match lobby_mgr.prepare_game() {
                        Some(mut handle) => {
                            let assigned_id = handle.account_id;
                            // Take the XMPP-side channel ends -- they'll be passed
                            // to activate_game after we know the real port.
                            let lobby_tx = handle.lobby_tx.take().unwrap();
                            let game_rx = handle.game_rx.take().unwrap();

                            match manager.create_game(
                                None,
                                GameConfig {
                                    password: String::new(),
                                    is_saved_game: false,
                                    use_lobby_auth: Some(true),
                                    lobby_rx: Some(handle.lobby_rx),
                                    lobby_tx: Some(handle.game_tx),
                                    server_name: game_name.clone(),
                                },
                                Some(assigned_id),
                            ) {
                                Ok((game_id, actual_port)) => {
                                    // Now notify the XMPP task with the real port
                                    lobby_mgr.activate_game(
                                        assigned_id,
                                        lobby_tx,
                                        game_rx,
                                        String::new(),
                                        actual_port,
                                    );
                                    log::info!(
                                        "Game {} created on port {} for '{}'",
                                        game_id,
                                        actual_port,
                                        sender
                                    );
                                    active_senders.insert(sender.clone());
                                    account_sender.insert(assigned_id, sender);
                                    let reply = format!(
                                        "{} created! please connect to control it.",
                                        game_name
                                    );
                                    lobby_mgr.send_muc_message(assigned_id, reply);
                                }
                                Err(e) => {
                                    log::error!("Failed to create game: {}", e);
                                    lobby_mgr.send_muc_message(
                                        assigned_id,
                                        format!("Failed to create game: {}", e),
                                    );
                                    // Release the account since game creation failed
                                    lobby_mgr.release_game(assigned_id);
                                }
                            }
                        }
                        None => {
                            log::warn!("No free XMPP accounts available for 'hostme' request");
                            lobby_mgr.send_muc_message(
                                account_id,
                                "No available slot - all games are in use".to_string(),
                            );
                        }
                    }
                }
                XmppToMainEvent::GameEnded {
                    account_id,
                    player_names,
                } => {
                    log::info!("Game ended for account {}", account_id);
                    if !player_names.is_empty() {
                        let names = player_names.join(", ");
                        let msg = format!(
                            "{} Thanks for testing dedicated server! Hosted game was ended.",
                            names
                        );
                        lobby_mgr.send_muc_message(account_id, msg);
                    }
                    if let Some(sender) = account_sender.remove(&account_id) {
                        active_senders.remove(&sender);
                    }
                    if let Some(game_id) = manager.find_game_by_account(account_id) {
                        manager.destroy_game(game_id);
                    }
                    lobby_mgr.release_game(account_id);
                }
            }
        }

        lobby_mgr.shutdown().await;
        log::info!("Main event loop ended, XMPP clients shut down.");
    } else {
        // No lobby configured: create a game immediately
        let (_game_id, actual_port) = manager
            .create_game(
                Some(port),
                GameConfig {
                    password: "".to_string(),
                    is_saved_game: false,
                    use_lobby_auth: Some(false),
                    lobby_rx: None,
                    lobby_tx: None,
                    server_name,
                },
                None,
            )
            .expect("Failed to create initial game");

        log::info!("Game running on port {} (no lobby)", actual_port);

        // Block until Ctrl-C
        tokio::signal::ctrl_c().await.ok();
    }
}

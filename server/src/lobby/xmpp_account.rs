use std::time::Duration;

use futures::stream::StreamExt;
use tokio::sync::mpsc;
use tokio_xmpp::Client;
use tokio_xmpp::parsers::{
    iq::Iq,
    jid::Jid,
    message::{Message as XmppMessage, MessageType},
    muc::Muc,
    presence::{Presence, Type as PresenceType},
};

use super::stanza_builders::*;
use super::types::*;

fn get_username(jid: &Jid) -> Option<String> {
    jid.node().map(|n| n.to_string())
}

fn build_muc_presence(muc_room: &str, bound_jid: &Jid) -> Presence {
    let nickname = get_username(bound_jid).unwrap_or_else(|| "dedicated".to_string());
    let to: Jid = format!("{}/{}", muc_room, nickname)
        .parse()
        .expect("invalid MUC JID");

    let mut presence = Presence::new(PresenceType::None);
    presence.from = Some(bound_jid.clone());
    presence.to = Some(to);
    // Add <x xmlns='http://jabber.org/protocol/muc'/> child
    let muc = Muc::new();
    presence.add_payload(muc);
    presence
}

async fn handle_iq(
    iq: Iq,
    client: &mut Client,
    lobby_tx: &Option<mpsc::Sender<LobbyToGameEvent>>,
    game_password: &str,
    game_port: u16,
    public_ip: &str,
) {
    match iq {
        Iq::Set {
            from,
            id,
            ref payload,
            ..
        } if payload.is("auth", NS_LOBBYAUTH) => {
            if let Some(token) = parse_lobby_auth(payload) {
                if let Some(ref from_jid) = from {
                    if let Some(username) = get_username(from_jid) {
                        if let Some(tx) = lobby_tx {
                            let _ = tx
                                .send(LobbyToGameEvent::LobbyAuth { username, token })
                                .await;
                        }
                    }
                }
            }
            // Always send IQ result back
            if let Some(from_jid) = from {
                let result = Iq::empty_result(from_jid, id);
                let _ = client.send_stanza(result.into()).await;
            }
        }

        Iq::Get {
            from,
            id,
            ref payload,
            ..
        } if payload.is("connectiondata", NS_CONNECTIONDATA) => {
            if let Some(from_jid) = from {
                match parse_connectiondata_request(payload) {
                    Some((password, client_salt, is_local)) => {
                        // Dedicated server doesn't support local IP discovery.
                        // Respond with error -- the client ignores this if already connected.
                        if is_local {
                            log::info!(
                                "connectiondata request with isLocalIP=1 from {:?}, responding with error",
                                from_jid
                            );
                            let error_elem = build_connectiondata_error("local_ip_failed");
                            let result = Iq::Result {
                                from: None,
                                to: Some(from_jid),
                                id,
                                payload: Some(error_elem),
                            };
                            let _ = client.send_stanza(result.into()).await;
                            return;
                        }

                        log::info!("connectiondata request from {:?}", from_jid);
                        let pw_ok = if game_password.is_empty() {
                            true
                        } else {
                            let gp = game_password.to_string();
                            let cs = client_salt.clone();
                            let expected = tokio::task::spawn_blocking(move || {
                                crate::utils::hash_cryptographically(&gp, &cs)
                            })
                            .await
                            .unwrap_or_default();
                            expected == password
                        };

                        if pw_ok {
                            let response_elem = build_connectiondata_response(public_ip, game_port);
                            let result = Iq::Result {
                                from: None,
                                to: Some(from_jid),
                                id,
                                payload: Some(response_elem),
                            };
                            let _ = client.send_stanza(result.into()).await;
                        } else {
                            let error_elem = build_connectiondata_error("PasswordWrong");
                            let result = Iq::Result {
                                from: None,
                                to: Some(from_jid),
                                id,
                                payload: Some(error_elem),
                            };
                            let _ = client.send_stanza(result.into()).await;
                        }
                    }
                    None => {
                        log::warn!("Malformed connectiondata request");
                    }
                }
            }
        }

        Iq::Result { from, id, .. } => {
            log::debug!("IQ result from {:?} id={}", from, id);
        }
        Iq::Error {
            from,
            id,
            ref error,
            ..
        } => {
            log::warn!(
                "IQ error from {:?} id={}: {:?} {:?}",
                from,
                id,
                error.type_,
                error.defined_condition
            );
        }
        _ => {}
    }
}

async fn handle_game_event(
    evt: GameToLobbyEvent,
    client: &mut Client,
    bot_jid_str: &str,
    bound_jid: &Option<Jid>,
) {
    let bot_jid: Jid = match bot_jid_str.parse() {
        Ok(j) => j,
        Err(e) => {
            log::error!("Invalid bot JID '{}': {}", bot_jid_str, e);
            return;
        }
    };

    match evt {
        GameToLobbyEvent::RegisterGame { mut attrs } => {
            // Inject hostJID -- the bot and clients need this to identify the host.
            if let Some(jid) = bound_jid {
                attrs.insert("hostJID".to_string(), jid.to_string());
            }
            // startTime is optional but expected by the client's StanzaKeys.
            attrs
                .entry("startTime".to_string())
                .or_insert_with(String::new);
            log::info!(
                "Sending RegisterGame to bot (name={:?}, nbp={:?})",
                attrs.get("name"),
                attrs.get("nbp"),
            );
            let query = build_register_game_query(&attrs);
            {
                let mut buf = Vec::<u8>::new();
                if let Ok(()) = query.write_to(&mut buf) {
                    log::debug!(
                        "RegisterGame IQ payload XML: {}",
                        String::from_utf8_lossy(&buf)
                    );
                }
            }
            let _token = client
                .send_iq(Some(bot_jid), tokio_xmpp::IqRequest::Set(query))
                .await;
        }
        GameToLobbyEvent::ChangeState {
            num_players,
            player_names,
        } => {
            let query = build_changestate_query(num_players, &player_names);
            let _token = client
                .send_iq(Some(bot_jid), tokio_xmpp::IqRequest::Set(query))
                .await;
        }
        GameToLobbyEvent::UnregisterGame => {
            let query = build_unregister_game_query();
            let _token = client
                .send_iq(Some(bot_jid), tokio_xmpp::IqRequest::Set(query))
                .await;
        }
        GameToLobbyEvent::GameEnded { .. } | GameToLobbyEvent::SendMucMessage { .. } => {
            // Handled directly in the drain loop, not here.
        }
    }
}

async fn handle_muc_message(
    msg: &XmppMessage,
    bound_jid: &Option<Jid>,
    main_tx: &mpsc::Sender<XmppToMainEvent>,
    account_id: usize,
    has_game: bool,
) {
    // Only process groupchat messages (MUC room messages)
    if msg.type_ != MessageType::Groupchat {
        return;
    }

    // Ignore messages from ourselves.
    // MUC from JID is "room@conference.example/nickname"; our nickname is the
    // node part of our bound JID.
    if let (Some(from), Some(bound)) = (&msg.from, bound_jid) {
        let our_nickname = get_username(bound).unwrap_or_default();
        if let Some(resource) = from.resource() {
            if resource.as_str() == our_nickname {
                return;
            }
        }
    }

    // Ignore delay-stamped (history) messages to avoid acting on old commands
    // after reconnect. We check for the delay payload by looking at payloads.
    let has_delay = msg.payloads.iter().any(|p| p.is("delay", "urn:xmpp:delay"));
    if has_delay {
        return;
    }

    // Extract body text (language-neutral first, then any language)
    let body = if let Some(b) = msg.bodies.get("") {
        b.trim().to_string()
    } else if let Some(b) = msg.bodies.values().next() {
        b.trim().to_string()
    } else {
        return;
    };

    if body == "hostme" {
        if has_game {
            log::debug!(
                "Account {} ignoring 'hostme' -- already hosting a game",
                account_id
            );
            return;
        }
        // Extract sender nickname from MUC JID (room@conf/Nickname -> "Nickname")
        let sender = msg
            .from
            .as_ref()
            .and_then(|jid| jid.resource())
            .map(|r| r.to_string())
            .unwrap_or_else(|| "unknown".to_string());
        log::info!(
            "Received 'hostme' command from '{}' in MUC (account {})",
            sender,
            account_id
        );
        let _ = main_tx
            .send(XmppToMainEvent::HostRequested { account_id, sender })
            .await;
    }
}

/// Main loop for a single XMPP account. Reconnects on disconnect with backoff.
pub async fn run_xmpp_account(
    creds: XmppCredentials,
    muc_room: String,
    bot_jid_str: String,
    public_ip: String,
    engine_version: String,
    mut control_rx: mpsc::Receiver<AccountControl>,
    account_id: usize,
    main_tx: mpsc::Sender<XmppToMainEvent>,
) {
    // The lobby bot filters out any IQ from a resource that doesn't start
    // with "0ad" (mirrors what XmppClient.cpp does: "/0ad-" + ps_generate_guid()).
    // If no resource is specified in the config, append one now.
    let jid_str = if creds.jid.contains('/') {
        creds.jid.clone()
    } else {
        format!("{}/0ad-{}", creds.jid, uuid::Uuid::new_v4())
    };
    let jid: Jid = jid_str
        .parse()
        .unwrap_or_else(|e| panic!("Invalid JID '{}': {:?}", jid_str, e));

    // 0 A.D. hashes passwords with PBKDF2 before XMPP auth -- the lobby server
    // stores the hashed form, so we must do the same.
    let username = get_username(&jid).unwrap_or_default();
    let hashed_password = crate::utils::encrypt_password(&creds.password, &username);

    let mut shutdown = false;

    loop {
        // (Re)connect
        log::info!("XMPP account {} connecting...", creds.jid);
        let mut client = Client::new(jid.clone(), hashed_password.clone());

        let mut lobby_tx: Option<mpsc::Sender<LobbyToGameEvent>> = None;
        let mut game_rx: Option<mpsc::Receiver<GameToLobbyEvent>> = None;
        let mut game_password = String::new();
        let mut game_port: u16 = 0;
        let mut is_online = false;
        let mut bound_jid: Option<Jid> = None;

        'inner: loop {
            // 1. Non-blocking drain of control messages
            let mut pending_muc_messages: Vec<String> = Vec::new();
            loop {
                match control_rx.try_recv() {
                    Ok(AccountControl::AssignGame {
                        lobby_tx: lt,
                        game_rx: gr,
                        password,
                        port,
                    }) => {
                        log::info!("XMPP account {} assigned game on port {}", creds.jid, port);
                        lobby_tx = Some(lt);
                        game_rx = Some(gr);
                        // Pre-hash the password the same way the C++ host does:
                        // HashCryptographically(password, hostJID + password + engine_version)
                        // This must happen before any client tries to connect.
                        let host_jid_str = bound_jid
                            .as_ref()
                            .map(|j| j.to_string())
                            .unwrap_or_default();
                        let salt = format!("{}{}{}", host_jid_str, password, engine_version);
                        game_password = crate::utils::hash_cryptographically(&password, &salt);
                        // Send the pre-hashed password to the game server so it can
                        // verify ENet-level authentication the same way C++ does.
                        if let Some(ref tx) = lobby_tx {
                            let _ = tx.try_send(LobbyToGameEvent::HashedPassword {
                                password: game_password.clone(),
                            });
                        }
                        game_port = port;
                    }
                    Ok(AccountControl::ReleaseGame) => {
                        log::info!("XMPP account {} released game", creds.jid);
                        lobby_tx = None;
                        game_rx = None;
                        game_password.clear();
                        game_port = 0;
                    }
                    Ok(AccountControl::SendMucMessage { body }) => {
                        pending_muc_messages.push(body);
                    }
                    Ok(AccountControl::Shutdown) => {
                        log::info!("XMPP account {} received shutdown signal", creds.jid);
                        shutdown = true;
                        break;
                    }
                    Err(mpsc::error::TryRecvError::Empty) => break,
                    Err(mpsc::error::TryRecvError::Disconnected) => {
                        log::info!(
                            "XMPP account {} control channel closed, shutting down",
                            creds.jid
                        );
                        shutdown = true;
                        break;
                    }
                }
            }
            if shutdown {
                break 'inner;
            }

            // 2. Non-blocking drain of game events (before sending MUC messages,
            //    so that SendMucMessage events are queued in time)
            if let Some(ref mut rx) = game_rx {
                let mut ended_player_names: Option<Vec<String>> = None;
                loop {
                    match rx.try_recv() {
                        Ok(GameToLobbyEvent::GameEnded { player_names }) => {
                            ended_player_names = Some(player_names);
                        }
                        Ok(GameToLobbyEvent::SendMucMessage { body }) => {
                            pending_muc_messages.push(body);
                        }
                        Ok(evt) => {
                            if is_online {
                                handle_game_event(evt, &mut client, &bot_jid_str, &bound_jid).await;
                            }
                        }
                        Err(mpsc::error::TryRecvError::Empty) => break,
                        Err(mpsc::error::TryRecvError::Disconnected) => {
                            log::info!(
                                "XMPP account {} game channel closed (game ended)",
                                creds.jid
                            );
                            game_rx = None;
                            lobby_tx = None;
                            // Flush any pending MUC messages before cleaning up
                            if is_online && !pending_muc_messages.is_empty() {
                                let muc_jid: Jid = muc_room.parse().expect("invalid MUC JID");
                                for body in pending_muc_messages.drain(..) {
                                    let msg = XmppMessage::groupchat(muc_jid.clone())
                                        .with_body("".into(), body);
                                    let _ = client.send_stanza(msg.into()).await;
                                }
                            }
                            let names = ended_player_names.take().unwrap_or_default();
                            let _ = main_tx
                                .send(XmppToMainEvent::GameEnded {
                                    account_id,
                                    player_names: names,
                                })
                                .await;
                            break;
                        }
                    }
                }
            }

            // Send any queued MUC messages (after draining game events)
            if is_online && !pending_muc_messages.is_empty() {
                let muc_jid: Jid = muc_room.parse().expect("invalid MUC JID");
                for body in pending_muc_messages.drain(..) {
                    let msg = XmppMessage::groupchat(muc_jid.clone()).with_body("".into(), body);
                    let _ = client.send_stanza(msg.into()).await;
                }
            }

            // 3. Wait for next XMPP event with short timeout
            match tokio::time::timeout(Duration::from_millis(50), client.next()).await {
                Ok(Some(event)) => {
                    if event.is_online() {
                        is_online = true;
                        let b = event.get_jid().cloned().unwrap_or_else(|| jid.clone());
                        log::info!("XMPP account {} online as {}", creds.jid, b);
                        bound_jid = Some(b.clone());

                        let presence = build_muc_presence(&muc_room, &b);
                        let _ = client.send_stanza(presence.into()).await;
                    } else if let Some(stanza) = event.into_stanza() {
                        match stanza {
                            tokio_xmpp::Stanza::Iq(iq) => {
                                log::debug!(
                                    "IQ received, bound_jid={:?}, game_password_len={}, engine_version={}",
                                    bound_jid.as_ref().map(|j| j.to_string()),
                                    game_password.len(),
                                    engine_version,
                                );
                                handle_iq(
                                    iq,
                                    &mut client,
                                    &lobby_tx,
                                    &game_password,
                                    game_port,
                                    &public_ip,
                                )
                                .await;
                            }
                            tokio_xmpp::Stanza::Message(msg) => {
                                handle_muc_message(
                                    &msg,
                                    &bound_jid,
                                    &main_tx,
                                    account_id,
                                    lobby_tx.is_some(),
                                )
                                .await;
                            }
                            _ => {} // Ignore presence stanzas
                        }
                    }
                }
                Ok(None) => {
                    // Stream ended
                    log::warn!("XMPP account {} stream ended", creds.jid);
                    is_online = false;
                    bound_jid = None;
                    break 'inner;
                }
                Err(_) => {
                    // Timeout -- loop back to drain control/game messages
                }
            }
        }

        if shutdown {
            log::info!("XMPP account {} shutting down", creds.jid);
            break;
        }

        // Reconnect with backoff
        log::info!("XMPP account {} reconnecting in 5s...", creds.jid);
        tokio::time::sleep(Duration::from_secs(5)).await;
    }
}

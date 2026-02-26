use std::{
    net::SocketAddr,
    sync::mpsc::{Receiver, Sender, TryRecvError},
    time::Duration,
};

use rusty_enet::{self as enet, Event};

use crate::network_message::{InboundNetworkMessage, OutboundNetworkMessage};

pub fn run_enet_host(
    bind_addr: SocketAddr,
    event_tx: Sender<InboundNetworkMessage>,
    send_rx: Receiver<OutboundNetworkMessage>,
    shutdown_rx: Receiver<()>,
) {
    let socket = std::net::UdpSocket::bind(bind_addr).expect("Failed to bind socket");

    let mut host = enet::Host::new(
        socket,
        enet::HostSettings {
            peer_limit: 32,
            channel_limit: 1,
            compressor: None,
            checksum: None,
            ..Default::default()
        },
    )
    .expect("Failed to create ENet host");

    log::info!("ENet server started on {}", bind_addr);

    loop {
        match shutdown_rx.try_recv() {
            Ok(_) | Err(TryRecvError::Disconnected) => {
                log::info!("ENet host on {} shutting down", bind_addr);
                break;
            }
            Err(TryRecvError::Empty) => {}
        }

        match host.service() {
            Ok(Some(event)) => {
                let addr = match &event {
                    Event::Connect { peer, data: _ } => peer,
                    Event::Disconnect { peer, data: _ } => peer,
                    Event::Receive {
                        peer,
                        channel_id: _,
                        packet: _,
                    } => peer,
                }
                .address()
                .unwrap()
                .ip();

                let inbound_message = match event.no_ref() {
                    rusty_enet::EventNoRef::Connect { peer, data: _ } => {
                        InboundNetworkMessage::Connect { ip: addr, peer }
                    }
                    rusty_enet::EventNoRef::Disconnect { peer, data } => {
                        InboundNetworkMessage::Disconnect {
                            ip: addr,
                            peer,
                            reason: data,
                        }
                    }
                    rusty_enet::EventNoRef::Receive {
                        peer,
                        channel_id: _,
                        packet,
                    } => InboundNetworkMessage::Message {
                        ip: addr,
                        peer,
                        data: packet.data().to_vec(),
                    },
                };

                if let Err(e) = event_tx.send(inbound_message) {
                    log::error!("Failed to send event to server: {:?}", e);
                    break;
                }
            }
            Ok(None) => {}
            Err(e) => {
                log::error!("ENet host error: {:?}", e);
            }
        }

        while let Ok(msg) = send_rx.try_recv() {
            match msg {
                OutboundNetworkMessage::Message { peer, data } => {
                    if let Some(peer) = host.get_peer_mut(peer) {
                        let packet = enet::Packet::new(data, enet::PacketKind::Reliable);
                        let _ = peer.send(0, &packet);
                    }
                }
                OutboundNetworkMessage::Disconnect { peer, reason } => {
                    if let Some(peer) = host.get_peer_mut(peer) {
                        peer.disconnect(reason);
                    }
                }
            }
        }

        std::thread::sleep(Duration::from_millis(10));
    }
}

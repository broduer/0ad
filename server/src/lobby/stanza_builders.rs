use tokio_xmpp::minidom::Element;
use tokio_xmpp::minidom::rxml::{Namespace as RxmlNamespace, NcName};

pub const NS_GAMELIST: &str = "jabber:iq:gamelist";
pub const NS_LOBBYAUTH: &str = "jabber:iq:lobbyauth";
pub const NS_CONNECTIONDATA: &str = "jabber:iq:connectiondata";

pub fn build_register_game_query(attrs: &std::collections::HashMap<String, String>) -> Element {
    let mut game = Element::bare("game", NS_GAMELIST);
    for (key, value) in attrs {
        let name = NcName::try_from(key.as_str()).expect("invalid attribute name");
        game.set_attr(RxmlNamespace::NONE, name, value);
    }

    Element::builder("query", NS_GAMELIST)
        .append(
            Element::builder("command", NS_GAMELIST)
                .append("register")
                .build(),
        )
        .append(game)
        .build()
}

pub fn build_unregister_game_query() -> Element {
    Element::builder("query", NS_GAMELIST)
        .append(
            Element::builder("command", NS_GAMELIST)
                .append("unregister")
                .build(),
        )
        .append(Element::bare("game", NS_GAMELIST))
        .build()
}

pub fn build_changestate_query(nbp: u32, players: &str) -> Element {
    let mut game = Element::bare("game", NS_GAMELIST);
    {
        game.set_attr(
            RxmlNamespace::NONE,
            NcName::try_from("nbp").unwrap(),
            nbp.to_string(),
        );
        game.set_attr(
            RxmlNamespace::NONE,
            NcName::try_from("players").unwrap(),
            players,
        );
    }

    Element::builder("query", NS_GAMELIST)
        .append(
            Element::builder("command", NS_GAMELIST)
                .append("changestate")
                .build(),
        )
        .append(game)
        .build()
}

pub fn build_connectiondata_response(ip: &str, port: u16) -> Element {
    Element::builder("connectiondata", NS_CONNECTIONDATA)
        .append(Element::builder("ip", NS_CONNECTIONDATA).append(ip).build())
        .append(
            Element::builder("port", NS_CONNECTIONDATA)
                .append(port.to_string())
                .build(),
        )
        .build()
}

pub fn build_connectiondata_error(error: &str) -> Element {
    Element::builder("connectiondata", NS_CONNECTIONDATA)
        .append(
            Element::builder("error", NS_CONNECTIONDATA)
                .append(error)
                .build(),
        )
        .build()
}

pub fn parse_lobby_auth(element: &Element) -> Option<String> {
    if !element.is("auth", NS_LOBBYAUTH) {
        return None;
    }
    element.get_child("token", NS_LOBBYAUTH).map(|t| t.text())
}

pub fn parse_connectiondata_request(element: &Element) -> Option<(String, String, bool)> {
    if !element.is("connectiondata", NS_CONNECTIONDATA) {
        return None;
    }
    let password = element
        .get_child("password", NS_CONNECTIONDATA)
        .map(|e| e.text())
        .unwrap_or_default();
    let clientsalt = element
        .get_child("clientsalt", NS_CONNECTIONDATA)
        .map(|e| e.text())
        .unwrap_or_default();
    let is_local = element
        .get_child("isLocalIP", NS_CONNECTIONDATA)
        .map(|e| e.text() == "true")
        .unwrap_or(false);
    Some((password, clientsalt, is_local))
}

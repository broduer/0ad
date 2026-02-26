use crate::messages::{ClientHandshake, ServerHandshake};

pub fn hash_cryptographically(string: &str, salt: &str) -> String {
    use hex;
    use sodiumoxide::crypto::generichash::State as GenericHashState;
    use sodiumoxide::crypto::pwhash::argon2id13::{self, Salt};
    use sodiumoxide::init;

    if string.is_empty() {
        return string.to_string();
    }

    assert!(init().is_ok());

    const SALT_SIZE: usize = 16; // matches crypto_pwhash_SALTBYTES
    const HASH_SIZE: usize = 32;

    // Start with static salt base
    let mut salt_buffer = [0u8; SALT_SIZE];
    salt_buffer[..16].copy_from_slice(&[
        235, 82, 29, 20, 135, 168, 184, 97, 7, 240, 48, 109, 8, 34, 158, 32,
    ]);

    // Hash the input salt with initial salt_buffer as key
    let mut state = GenericHashState::new(Some(SALT_SIZE), Some(&salt_buffer)).unwrap();
    state.update(salt.as_bytes()).unwrap();
    let hashed_bytes = state.finalize().unwrap();
    salt_buffer.copy_from_slice(&hashed_bytes[..SALT_SIZE]);

    let final_salt = Salt::from_slice(&salt_buffer).unwrap();
    let mut output = [0u8; HASH_SIZE];

    // Must match the C++ HashCryptographically parameters exactly:
    // opsLimit = 2 (crypto_pwhash_argon2id_OPSLIMIT_INTERACTIVE)
    // memLimit = 32768 (4 * crypto_pwhash_argon2id_MEMLIMIT_MIN)
    argon2id13::derive_key(
        &mut output,
        string.as_bytes(),
        &final_salt,
        argon2id13::OpsLimit(2),
        argon2id13::MemLimit(32768),
    )
    .expect("Hashing failed");

    hex::encode_upper(output)
}

/// Replicate the 0 A.D. `EncryptPassword()` from JSInterface_Lobby.cpp.
///
/// The game client hashes the plaintext password with this function *before*
/// XMPP SASL authentication.  The lobby server therefore stores the hashed
/// form, so the dedicated server must do the same transformation.
///
/// Algorithm:
///   salt = SHA-256(static_salt_base || username)
///   output = PBKDF2-HMAC*-SHA256(password, salt, 1337 rounds)
///            (* non-standard HMAC with 32-byte block, see pkcs5_pbkdf2.cpp)
///   result = uppercase hex of output
pub fn encrypt_password(password: &str, username: &str) -> String {
    use sodiumoxide::crypto::hash::sha256;
    use sodiumoxide::init;

    assert!(init().is_ok());

    const DIGEST_SIZE: usize = 32;
    const ITERATIONS: u32 = 1337;

    static SALT_BASE: [u8; DIGEST_SIZE] = [
        244, 243, 249, 244, 32, 33, 34, 35, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 32, 33,
        244, 224, 127, 129, 130, 140, 153, 133, 123, 234, 123,
    ];

    // 1. Derive salt = SHA256(salt_base || username)
    let mut state = sha256::State::new();
    state.update(&SALT_BASE);
    state.update(username.as_bytes());
    let salt = state.finalize();

    // 2. PBKDF2 with non-standard HMAC (32-byte block size) -- single block (count=1)
    let key = password.as_bytes();
    let mut asalt = [0u8; DIGEST_SIZE + 4];
    asalt[..DIGEST_SIZE].copy_from_slice(&salt.0);
    // count = 1 in big-endian
    asalt[DIGEST_SIZE] = 0;
    asalt[DIGEST_SIZE + 1] = 0;
    asalt[DIGEST_SIZE + 2] = 0;
    asalt[DIGEST_SIZE + 3] = 1;

    let mut d1 = hmac_sha256_nonstandard(&asalt[..DIGEST_SIZE + 4], key);
    let mut obuf = d1;

    for _ in 1..ITERATIONS {
        let d2 = hmac_sha256_nonstandard(&d1, key);
        d1 = d2;
        for j in 0..DIGEST_SIZE {
            obuf[j] ^= d1[j];
        }
    }

    hex::encode_upper(obuf)
}

/// Non-standard HMAC-SHA256 matching 0 A.D.'s pkcs5_pbkdf2.cpp.
///
/// Uses 32-byte (DIGEST_SIZE) padding blocks instead of the standard 64-byte
/// block size.  This matches the original C++ implementation exactly.
fn hmac_sha256_nonstandard(text: &[u8], key: &[u8]) -> [u8; 32] {
    use sodiumoxide::crypto::hash::sha256;

    const DIGEST_SIZE: usize = 32;

    let (actual_key, actual_key_len) = if key.len() > DIGEST_SIZE {
        let h = sha256::hash(key);
        (h.0, DIGEST_SIZE)
    } else {
        let mut buf = [0u8; DIGEST_SIZE];
        buf[..key.len()].copy_from_slice(key);
        (buf, key.len())
    };

    // Inner: k_pad = key ^ 0x36
    let mut k_pad = [0u8; DIGEST_SIZE];
    k_pad[..actual_key_len].copy_from_slice(&actual_key[..actual_key_len]);
    for b in &mut k_pad {
        *b ^= 0x36;
    }
    let mut state = sha256::State::new();
    state.update(&k_pad);
    state.update(text);
    let inner = state.finalize();

    // Outer: k_pad = key ^ 0x5c
    let mut k_pad = [0u8; DIGEST_SIZE];
    k_pad[..actual_key_len].copy_from_slice(&actual_key[..actual_key_len]);
    for b in &mut k_pad {
        *b ^= 0x5c;
    }
    let mut state = sha256::State::new();
    state.update(&k_pad);
    state.update(&inner.0);
    let outer = state.finalize();

    outer.0
}

pub fn hexify(s: &[u8]) -> String {
    s.iter()
        .map(|c| format!("{:02x}", c))
        .collect::<Vec<String>>()
        .concat()
}

pub fn hex_dump(data: &[u8]) -> String {
    const BYTES_PER_LINE: usize = 8;

    let mut out = String::new();

    for (i, chunk) in data.chunks(BYTES_PER_LINE).enumerate() {
        // Offset
        out.push_str(&format!("{:08x}  ", i * BYTES_PER_LINE));

        // Hex section
        for j in 0..BYTES_PER_LINE {
            if j < chunk.len() {
                out.push_str(&format!("{:02x} ", chunk[j]));
            } else {
                out.push_str("   ");
            }

            if j == 7 {
                out.push(' ');
            }
        }

        out.push_str(" |");

        // ASCII section
        for &b in chunk {
            let c = if b.is_ascii_graphic() || b == b' ' {
                b as char
            } else {
                '.'
            };
            out.push(c);
        }

        out.push_str("|\n");
    }

    out
}

// 'P', 'c', 0x01, '!' -- fixed response magic sent by all clients (see NetMessages.h)
const PS_PROTOCOL_MAGIC_RESPONSE: u32 = 0x50630121;

pub fn handshake_is_correct(_server: &ServerHandshake, client: &ClientHandshake) -> bool {
    // Dedicated server: engine version and mods are defined by the controller (first client).
    // Only verify that the client sent the correct fixed magic response constant.
    if client.magic_response != PS_PROTOCOL_MAGIC_RESPONSE {
        log::warn!(
            "Handshake magic mismatch: expected {:#010x} got {:#010x}",
            PS_PROTOCOL_MAGIC_RESPONSE,
            client.magic_response
        );
        return false;
    }
    true
}

/// Validates a connecting client's engine version and mod list against the controller's.
/// Called for every client except the controller itself.
pub fn handshake_matches_controller(
    client: &ClientHandshake,
    controller_engine_version: &str,
    controller_mods: &[crate::messages::EnabledMod],
) -> bool {
    if client.engine_version != controller_engine_version {
        log::warn!(
            "Handshake engine version mismatch: controller={} client={}",
            controller_engine_version,
            client.engine_version
        );
        return false;
    }

    for i in 0..controller_mods.len().max(client.enabled_mods.len()) {
        let ctrl_mod = controller_mods
            .get(i)
            .map(|m| format!("{}-{}", m.name, m.version));
        let cli_mod = client
            .enabled_mods
            .get(i)
            .map(|m| format!("{}-{}", m.name, m.version));
        if ctrl_mod != cli_mod {
            log::warn!(
                "Handshake mod mismatch at index {}: controller={:?} client={:?}",
                i,
                ctrl_mod,
                cli_mod
            );
            return false;
        }
    }

    true
}

pub fn read_wide_string(buffer: &[u8], start_pos: usize) -> Result<(String, usize), String> {
    let mut pos = start_pos;
    let mut out = String::new();

    loop {
        if buffer.len() < pos + 2 {
            return Err("Buffer too short while reading wide string".into());
        }

        let unit = u16::from_be_bytes([buffer[pos], buffer[pos + 1]]);
        pos += 2;

        if unit == 0 {
            break; // null terminator found
        }

        let ch = if (0xD800..=0xDFFF).contains(&unit) {
            '\u{FFFD}'
        } else {
            char::from_u32(unit as u32).unwrap_or('\u{FFFD}')
        };

        out.push(ch);
    }

    Ok((out, pos))
}

pub fn write_wide_string(s: &str) -> Vec<u8> {
    let mut out = Vec::with_capacity(s.len() * 2 + 2);

    for ch in s.chars() {
        // characters above U+FFFF become replacement character
        let unit: u16 = if (ch as u32) <= 0xFFFF {
            ch as u16
        } else {
            0xFFFD
        };

        // big-endian
        out.push((unit >> 8) as u8);
        out.push((unit & 0xFF) as u8);
    }

    // null terminator
    out.push(0);
    out.push(0);

    out
}

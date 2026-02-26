#[derive(Debug)]
pub struct SyncError {
    pub turn: u32,
    pub hash_expected: Vec<u8>,
    pub player_names: Vec<String>,
}

impl SyncError {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        bytes.extend_from_slice(&self.turn.to_be_bytes());

        let hash_bytes = &self.hash_expected;
        bytes.extend_from_slice(&(hash_bytes.len() as u32).to_be_bytes());
        bytes.extend_from_slice(hash_bytes);

        bytes.extend_from_slice(&(self.player_names.len() as u32).to_be_bytes());
        for name in &self.player_names {
            let name_bytes = name.as_bytes();
            bytes.extend_from_slice(&(name_bytes.len() as u32).to_be_bytes());
            bytes.extend_from_slice(name_bytes);
        }

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        let mut pos = 0;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for turn".into());
        }
        let turn = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]);
        pos += 4;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for hash_expected length".into());
        }
        let hash_len = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]) as usize;
        pos += 4;

        if buffer.len() < pos + hash_len {
            return Err("Buffer too short for hash_expected data".into());
        }
        let hash_expected = buffer[pos..pos + hash_len].to_vec();
        pos += hash_len;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for player_names count".into());
        }
        let player_names_count = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]) as usize;
        pos += 4;

        let mut player_names = Vec::new();
        for _ in 0..player_names_count {
            if buffer.len() < pos + 4 {
                return Err("Buffer too short for player name length".into());
            }
            let name_len = u32::from_be_bytes([
                buffer[pos],
                buffer[pos + 1],
                buffer[pos + 2],
                buffer[pos + 3],
            ]) as usize;
            pos += 4;

            if buffer.len() < pos + name_len {
                return Err("Buffer too short for player name data".into());
            }
            player_names.push(
                String::from_utf8(buffer[pos..pos + name_len].to_vec())
                    .map_err(|e| format!("Invalid UTF-8 in player name: {}", e))?,
            );
            pos += name_len;
        }

        Ok(Self {
            turn,
            hash_expected,
            player_names,
        })
    }
}

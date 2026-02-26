#[derive(Debug)]
pub struct SyncCheck {
    pub turn: u32,
    pub hash: Vec<u8>,
}

impl SyncCheck {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        bytes.extend_from_slice(&self.turn.to_be_bytes());

        let hash_bytes = &self.hash;
        bytes.extend_from_slice(&(hash_bytes.len() as u32).to_be_bytes());
        bytes.extend_from_slice(hash_bytes);

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
            return Err("Buffer too short for hash length".into());
        }
        let hash_len = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]) as usize;
        pos += 4;

        if buffer.len() < pos + hash_len {
            return Err("Buffer too short for hash data".into());
        }
        let hash = buffer[pos..pos + hash_len].to_vec();

        pos += hash_len;

        Ok(Self { turn, hash })
    }
}

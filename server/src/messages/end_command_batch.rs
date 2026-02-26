#[derive(Debug)]
pub struct EndCommandBatch {
    pub turn: u32,
    pub turn_length: u16,
}

impl EndCommandBatch {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        bytes.extend_from_slice(&self.turn.to_be_bytes());
        bytes.extend_from_slice(&self.turn_length.to_be_bytes());

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

        if buffer.len() < pos + 2 {
            return Err("Buffer too short for turn_length".into());
        }
        let turn_length = u16::from_be_bytes([buffer[pos], buffer[pos + 1]]);
        pos += 2;

        Ok(Self { turn, turn_length })
    }
}

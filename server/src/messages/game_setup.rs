#[derive(Debug)]
pub struct GameSetup {
    pub data: Vec<u8>,
}

impl GameSetup {
    pub fn to_bytes(&self) -> Vec<u8> {
        self.data.clone()
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        Ok(Self {
            data: buffer.to_vec(),
        })
    }
}

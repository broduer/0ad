#[derive(Debug)]
pub struct GameSavedStart {
    pub init_attributes: String,
}

impl GameSavedStart {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        let attr_bytes = self.init_attributes.as_bytes();
        bytes.extend_from_slice(&(attr_bytes.len() as u32).to_be_bytes());
        bytes.extend_from_slice(attr_bytes);

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        let mut pos = 0;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for init_attributes length".into());
        }
        let attr_len = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]) as usize;
        pos += 4;

        if buffer.len() < pos + attr_len {
            return Err("Buffer too short for init_attributes data".into());
        }
        let init_attributes = String::from_utf8(buffer[pos..pos + attr_len].to_vec())
            .map_err(|e| format!("Invalid UTF-8 in init_attributes: {}", e))?;
        pos += attr_len;

        Ok(Self { init_attributes })
    }
}

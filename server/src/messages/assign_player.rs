use super::guid::Guid;

#[derive(Debug)]
pub struct AssignPlayer {
    pub player_id: i8,
    pub guid: Guid,
}

impl AssignPlayer {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        bytes.push(self.player_id as u8);

        let guid_string = self.guid.to_string();
        let guid_bytes = guid_string.as_bytes();
        bytes.extend_from_slice(&(guid_bytes.len() as u32).to_be_bytes());
        bytes.extend_from_slice(guid_bytes);

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        let mut pos = 0;

        if buffer.len() < pos + 1 {
            return Err("Buffer too short for player_id".into());
        }
        let player_id = buffer[pos] as i8;
        pos += 1;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for guid length".into());
        }
        let guid_len = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]) as usize;
        pos += 4;

        if buffer.len() < pos + guid_len {
            return Err("Buffer too short for guid data".into());
        }
        let guid = Guid(
            String::from_utf8(buffer[pos..pos + guid_len].to_vec())
                .map_err(|e| format!("Invalid UTF-8 in guid: {}", e))?,
        );
        pos += guid_len;

        Ok(Self { player_id, guid })
    }
}

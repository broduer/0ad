use super::guid::Guid;
use crate::utils::{read_wide_string, write_wide_string};

#[derive(Debug)]
pub struct Host {
    pub guid: Guid,
    pub name: String,
    pub player_id: i8,
    pub status: u8,
}

impl Host {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        let guid_string = self.guid.to_string();
        let guid_bytes = guid_string.as_bytes();
        bytes.extend_from_slice(&(guid_bytes.len() as u32).to_be_bytes());
        bytes.extend_from_slice(guid_bytes);

        let name_bytes = write_wide_string(&self.name);
        bytes.extend_from_slice(&name_bytes);

        bytes.push(self.player_id as u8);
        bytes.push(self.status);

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<(Self, usize), String> {
        let mut pos = 0;

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

        let (name, mut pos) = read_wide_string(buffer, pos)?;

        if buffer.len() < pos + 1 {
            return Err("Buffer too short for player_id".into());
        }
        let player_id = buffer[pos] as i8;
        pos += 1;

        if buffer.len() < pos + 1 {
            return Err("Buffer too short for status".into());
        }
        let status = buffer[pos];
        pos += 1;

        Ok((
            Self {
                guid,
                name,
                player_id,
                status,
            },
            pos,
        ))
    }
}

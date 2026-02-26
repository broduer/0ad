use super::guid::Guid;

#[derive(Debug)]
pub struct ClientsLoading {
    pub clients: Vec<Guid>,
}

impl ClientsLoading {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        bytes.extend_from_slice(&(self.clients.len() as u32).to_be_bytes());
        for guid in &self.clients {
            let guid_string = guid.to_string();
            let guid_bytes = guid_string.as_bytes();
            bytes.extend_from_slice(&(guid_bytes.len() as u32).to_be_bytes());
            bytes.extend_from_slice(guid_bytes);
        }

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        let mut pos = 0;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for clients count".into());
        }
        let clients_count = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]) as usize;
        pos += 4;

        let mut clients = Vec::new();
        for _ in 0..clients_count {
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
            clients.push(Guid(
                String::from_utf8(buffer[pos..pos + guid_len].to_vec())
                    .map_err(|e| format!("Invalid UTF-8 in guid: {}", e))?,
            ));
            pos += guid_len;
        }

        Ok(Self { clients })
    }
}

use super::performance_entry::PerformanceEntry;

#[derive(Debug)]
pub struct ClientPerformance {
    pub clients: Vec<PerformanceEntry>,
}

impl ClientPerformance {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        bytes.extend_from_slice(&(self.clients.len() as u32).to_be_bytes());
        for client in &self.clients {
            bytes.extend(client.to_bytes());
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
            let (client, new_pos) = PerformanceEntry::from_bytes(buffer, pos)?;
            clients.push(client);
            pos = new_pos;
        }

        Ok(Self { clients })
    }
}

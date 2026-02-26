#[derive(Debug)]
pub struct FileTransferRequest {
    pub request_type: i8,
    pub request_id: u32,
}

impl FileTransferRequest {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        bytes.push(self.request_type as u8);
        bytes.extend_from_slice(&self.request_id.to_be_bytes());

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        let mut pos = 0;

        if buffer.len() < pos + 1 {
            return Err("Buffer too short for request_type".into());
        }
        let request_type = buffer[pos] as i8;
        pos += 1;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for request_id".into());
        }
        let request_id = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]);
        pos += 4;

        Ok(Self {
            request_type,
            request_id,
        })
    }
}

use super::host::Host;

#[derive(Debug)]
pub struct PlayerAssignment {
    pub hosts: Vec<Host>,
}

impl PlayerAssignment {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        for host in &self.hosts {
            bytes.extend(host.to_bytes());
        }

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        let mut pos = 0;

        let mut hosts = Vec::new();
        while pos < buffer.len() {
            let (host_item, bytes_read) = Host::from_bytes(&buffer[pos..])
                .map_err(|e| format!("Failed to read mod: {}", e))?;
            hosts.push(host_item);
            pos += bytes_read;
        }

        Ok(Self { hosts })
    }
}

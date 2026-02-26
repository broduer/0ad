use crate::utils::read_wide_string;

#[derive(Debug)]
pub struct Authenticate {
    pub name: String,
    pub password: String,
    pub controller_secret: String,
}

impl Authenticate {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        let name_bytes = self.name.as_bytes();
        bytes.extend_from_slice(&(name_bytes.len() as u32).to_be_bytes());
        bytes.extend_from_slice(name_bytes);

        let password_bytes = self.password.as_bytes();
        bytes.extend_from_slice(&(password_bytes.len() as u32).to_be_bytes());
        bytes.extend_from_slice(password_bytes);

        let secret_bytes = self.controller_secret.as_bytes();
        bytes.extend_from_slice(&(secret_bytes.len() as u32).to_be_bytes());
        bytes.extend_from_slice(secret_bytes);

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        let pos = 0;

        let (name, mut pos) = read_wide_string(buffer, pos).unwrap();

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for password length".into());
        }
        let password_len = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]) as usize;
        pos += 4;

        if buffer.len() < pos + password_len {
            return Err("Buffer too short for password data".into());
        }
        let password = String::from_utf8(buffer[pos..pos + password_len].to_vec())
            .map_err(|e| format!("Invalid UTF-8 in password: {}", e))?;
        pos += password_len;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for controller_secret length".into());
        }
        let secret_len = u32::from_be_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]) as usize;
        pos += 4;

        if buffer.len() < pos + secret_len {
            return Err("Buffer too short for controller_secret data".into());
        }
        let controller_secret = String::from_utf8(buffer[pos..pos + secret_len].to_vec())
            .map_err(|e| format!("Invalid UTF-8 in controller_secret: {}", e))?;
        pos += secret_len;

        Ok(Self {
            name,
            password,
            controller_secret,
        })
    }
}

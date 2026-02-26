#[derive(Debug, Clone)]
pub struct Simulation {
    pub client: u32,
    pub player: i32,
    pub turn: u32,
    pub data: Vec<u8>,
}

impl Simulation {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::new();

        bytes.extend_from_slice(&self.client.to_le_bytes());
        bytes.extend_from_slice(&self.player.to_le_bytes());
        bytes.extend_from_slice(&self.turn.to_le_bytes());

        let data_bytes = &self.data;
        bytes.extend_from_slice(data_bytes);

        bytes
    }

    pub fn from_bytes(buffer: &[u8]) -> Result<Self, String> {
        let mut pos = 0;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for client".into());
        }
        let client = u32::from_le_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]);
        pos += 4;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for player".into());
        }
        let player = i32::from_le_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]);
        pos += 4;

        if buffer.len() < pos + 4 {
            return Err("Buffer too short for turn".into());
        }
        let turn = u32::from_le_bytes([
            buffer[pos],
            buffer[pos + 1],
            buffer[pos + 2],
            buffer[pos + 3],
        ]);
        pos += 4;

        let data = buffer[pos..].to_vec();

        Ok(Self {
            client,
            player,
            turn,
            data,
        })
    }

    /// Extract the "type" property from a SpiderMonkey-serialized command object.
    /// Returns None if parsing fails or the object doesn't have a string "type" property.
    pub fn extract_command_type(data: &[u8]) -> Option<String> {
        if data.first()? != &SCRIPT_TYPE_OBJECT {
            return None;
        }
        let mut pos = 1;

        let num_props = read_u32(data, &mut pos)?;

        for _ in 0..num_props {
            // Each property: ScriptString(name) + ScriptVal(value)
            let (name, _) = read_script_string(data, &mut pos)?;
            if name == "type" {
                if data.get(pos)? == &SCRIPT_TYPE_STRING {
                    pos += 1;
                    let (val, _) = read_script_string(data, &mut pos)?;
                    return Some(val);
                } else {
                    return None;
                }
            } else {
                pos = skip_script_val(data, pos)?;
            }
        }

        None
    }
}

const SCRIPT_TYPE_VOID: u8 = 0x00;
const SCRIPT_TYPE_NULL: u8 = 0x01;
const SCRIPT_TYPE_ARRAY: u8 = 0x02;
const SCRIPT_TYPE_OBJECT: u8 = 0x03;
const SCRIPT_TYPE_STRING: u8 = 0x04;
const SCRIPT_TYPE_INT: u8 = 0x05;
const SCRIPT_TYPE_DOUBLE: u8 = 0x06;
const SCRIPT_TYPE_BOOLEAN: u8 = 0x07;
const SCRIPT_TYPE_BACKREF: u8 = 0x08;

fn read_u32(data: &[u8], pos: &mut usize) -> Option<u32> {
    if *pos + 4 > data.len() {
        return None;
    }
    let val = u32::from_le_bytes([data[*pos], data[*pos + 1], data[*pos + 2], data[*pos + 3]]);
    *pos += 4;
    Some(val)
}

/// Parse a ScriptString: u8 isLatin1 + u32 LE length + raw bytes
fn read_script_string(data: &[u8], pos: &mut usize) -> Option<(String, usize)> {
    let is_latin1 = *data.get(*pos)?;
    *pos += 1;

    let len = read_u32(data, pos)? as usize;

    if is_latin1 != 0 {
        if *pos + len > data.len() {
            return None;
        }
        let s: String = data[*pos..*pos + len].iter().map(|&b| b as char).collect();
        *pos += len;
        Some((s, *pos))
    } else {
        let byte_len = len * 2;
        if *pos + byte_len > data.len() {
            return None;
        }
        let utf16: Vec<u16> = (0..len)
            .map(|i| u16::from_le_bytes([data[*pos + i * 2], data[*pos + i * 2 + 1]]))
            .collect();
        let s = String::from_utf16_lossy(&utf16);
        *pos += byte_len;
        Some((s, *pos))
    }
}

/// Skip over a ScriptVal without fully parsing it.
fn skip_script_val(data: &[u8], mut pos: usize) -> Option<usize> {
    let tag = *data.get(pos)?;
    pos += 1;

    match tag {
        SCRIPT_TYPE_VOID | SCRIPT_TYPE_NULL => Some(pos),
        SCRIPT_TYPE_BOOLEAN => Some(pos + 1),
        SCRIPT_TYPE_INT | SCRIPT_TYPE_BACKREF => {
            if pos + 4 > data.len() {
                return None;
            }
            Some(pos + 4)
        }
        SCRIPT_TYPE_DOUBLE => {
            if pos + 8 > data.len() {
                return None;
            }
            Some(pos + 8)
        }
        SCRIPT_TYPE_STRING => {
            let (_, new_pos) = read_script_string(data, &mut pos)?;
            Some(new_pos)
        }
        SCRIPT_TYPE_OBJECT => {
            let num_props = read_u32(data, &mut pos)?;
            for _ in 0..num_props {
                let (_, _) = read_script_string(data, &mut pos)?;
                pos = skip_script_val(data, pos)?;
            }
            Some(pos)
        }
        SCRIPT_TYPE_ARRAY => {
            let length = read_u32(data, &mut pos)?;
            for _ in 0..length {
                pos = skip_script_val(data, pos)?;
            }
            let num_props = read_u32(data, &mut pos)?;
            for _ in 0..num_props {
                let (_, _) = read_script_string(data, &mut pos)?;
                pos = skip_script_val(data, pos)?;
            }
            Some(pos)
        }
        _ => None,
    }
}

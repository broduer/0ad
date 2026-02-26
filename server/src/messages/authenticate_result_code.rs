#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub enum AuthenticateResultCode {
    ARC_OK,
    ARC_OK_SAVED_GAME,
    ARC_OK_REJOINING,
    ARC_PASSWORD_INVALID,
}

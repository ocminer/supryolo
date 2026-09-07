// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
//! Narrow C ABI around pinned SRI Noise. No unauthenticated handshake option.
use noise_sv2::{Initiator, NoiseEngine, INITIATOR_EXPECTED_HANDSHAKE_MESSAGE_SIZE};
use std::{
    panic::{catch_unwind, AssertUnwindSafe},
    ptr, slice,
};
struct State {
    initiator: Option<Box<Initiator>>,
    engine: Option<NoiseEngine>,
    failed: bool,
}
#[no_mangle]
pub unsafe extern "C" fn yolo_noise_new(key: *const u8, first: *mut u8) -> *mut std::ffi::c_void {
    if key.is_null() || first.is_null() {
        return ptr::null_mut();
    }
    catch_unwind(|| {
        let key: [u8; 32] = slice::from_raw_parts(key, 32).try_into().ok()?;
        let mut initiator = Initiator::from_raw_k(key).ok()?;
        let message = initiator.step_0().ok()?;
        ptr::copy_nonoverlapping(message.as_ptr(), first, 64);
        Some(Box::into_raw(Box::new(State {
            initiator: Some(initiator),
            engine: None,
            failed: false,
        })) as *mut std::ffi::c_void)
    })
    .ok()
    .flatten()
    .unwrap_or(ptr::null_mut())
}
#[no_mangle]
pub unsafe extern "C" fn yolo_noise_finish(
    raw: *mut std::ffi::c_void,
    message: *const u8,
    len: usize,
) -> i32 {
    if raw.is_null() || message.is_null() || len != INITIATOR_EXPECTED_HANDSHAKE_MESSAGE_SIZE {
        return -1;
    }
    let s = &mut *(raw as *mut State);
    let result = catch_unwind(AssertUnwindSafe(|| {
        if s.failed {
            return None;
        }
        let msg: [u8; INITIATOR_EXPECTED_HANDSHAKE_MESSAGE_SIZE] =
            slice::from_raw_parts(message, len).try_into().ok()?;
        s.engine = Some(s.initiator.take()?.step_2(msg).ok()?);
        Some(())
    }));
    if matches!(result, Ok(Some(()))) {
        0
    } else {
        s.failed = true;
        -1
    }
}
#[no_mangle]
pub unsafe extern "C" fn yolo_noise_transform(
    raw: *mut std::ffi::c_void,
    decrypt: bool,
    input: *const u8,
    len: usize,
    output: *mut u8,
    capacity: usize,
) -> i32 {
    if raw.is_null()
        || input.is_null()
        || output.is_null()
        || len > 65535
        || (!decrypt && len > 65519)
        || capacity < len + 16
    {
        return -1;
    }
    let s = &mut *(raw as *mut State);
    let result = catch_unwind(AssertUnwindSafe(|| {
        if s.failed {
            return None;
        }
        let mut bytes = slice::from_raw_parts(input, len).to_vec();
        let engine = s.engine.as_mut()?;
        if decrypt {
            engine.decrypt(&mut bytes).ok()?;
        } else {
            engine.encrypt(&mut bytes).ok()?;
        }
        ptr::copy_nonoverlapping(bytes.as_ptr(), output, bytes.len());
        Some(bytes.len() as i32)
    }));
    match result {
        Ok(Some(n)) => n,
        _ => {
            s.failed = true;
            -1
        }
    }
}
#[no_mangle]
pub unsafe extern "C" fn yolo_noise_free(raw: *mut std::ffi::c_void) {
    if !raw.is_null() {
        drop(Box::from_raw(raw as *mut State));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use noise_sv2::Responder;
    use std::time::Duration;
    // Public test-only secp256k1 generator (private scalar = 1).
    fn key() -> [u8; 32] {
        [
            0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac, 0x55, 0xa0, 0x62, 0x95, 0xce, 0x87,
            0x0b, 0x07, 0x02, 0x9b, 0xfc, 0xdb, 0x2d, 0xce, 0x28, 0xd9, 0x59, 0xf2, 0x81, 0x5b,
            0x16, 0xf8, 0x17, 0x98,
        ]
    }
    unsafe fn pair() -> (*mut std::ffi::c_void, NoiseEngine) {
        let mut first = [0; 64];
        let client = yolo_noise_new(key().as_ptr(), first.as_mut_ptr());
        assert!(!client.is_null());
        let mut secret = [0; 32];
        secret[31] = 1;
        let mut responder =
            Responder::from_authority_kp(&key(), &secret, Duration::from_secs(60)).unwrap();
        let (response, engine) = responder.step_1(first).unwrap();
        assert_eq!(
            yolo_noise_finish(client, response.as_ptr(), response.len()),
            0
        );
        (client, engine)
    }
    #[test]
    fn ffi_round_trip_and_tamper_is_terminal() {
        unsafe {
            let (client, mut server) = pair();
            for n in [6, 1, 65519, 17] {
                let plain = vec![0xa5; n];
                let mut out = vec![0; n + 16];
                assert_eq!(
                    yolo_noise_transform(
                        client,
                        false,
                        plain.as_ptr(),
                        n,
                        out.as_mut_ptr(),
                        out.len()
                    ),
                    (n + 16) as i32
                );
                server.decrypt(&mut out).unwrap();
                assert_eq!(out, plain);
                server.encrypt(&mut out).unwrap();
                let mut decoded = vec![0; out.len() + 16];
                assert_eq!(
                    yolo_noise_transform(
                        client,
                        true,
                        out.as_ptr(),
                        out.len(),
                        decoded.as_mut_ptr(),
                        decoded.len()
                    ),
                    n as i32
                );
                assert_eq!(&decoded[..n], plain);
            }
            let mut packet = vec![1, 2, 3];
            server.encrypt(&mut packet).unwrap();
            packet[0] ^= 1;
            let mut out = [0; 64];
            assert_eq!(
                yolo_noise_transform(
                    client,
                    true,
                    packet.as_ptr(),
                    packet.len(),
                    out.as_mut_ptr(),
                    out.len()
                ),
                -1
            );
            packet[0] ^= 1;
            assert_eq!(
                yolo_noise_transform(
                    client,
                    true,
                    packet.as_ptr(),
                    packet.len(),
                    out.as_mut_ptr(),
                    out.len()
                ),
                -1
            );
            yolo_noise_free(client);
        }
    }
    #[test]
    fn wrong_authority_rejected() {
        unsafe {
            // Another valid public point, scalar 2; not the authority used below.
            let other = [
                0xc6, 0x04, 0x7f, 0x94, 0x41, 0xed, 0x7d, 0x6d, 0x30, 0x45, 0x40, 0x6e, 0x95, 0xc0,
                0x7c, 0xd8, 0x5c, 0x77, 0x8e, 0x4b, 0x8c, 0xef, 0x3c, 0xa7, 0xab, 0xac, 0x09, 0xb9,
                0x5c, 0x70, 0x9e, 0xe5,
            ];
            let mut first = [0; 64];
            let client = yolo_noise_new(other.as_ptr(), first.as_mut_ptr());
            assert!(!client.is_null());
            let mut secret = [0; 32];
            secret[31] = 1;
            let mut server =
                Responder::from_authority_kp(&key(), &secret, Duration::from_secs(60)).unwrap();
            let (response, _) = server.step_1(first).unwrap();
            assert_eq!(
                yolo_noise_finish(client, response.as_ptr(), response.len()),
                -1
            );
            yolo_noise_free(client);
        }
    }
    #[test]
    fn invalid_key_and_missing_handshake_rejected() {
        unsafe {
            let mut first = [0; 64];
            assert!(yolo_noise_new([255u8; 32].as_ptr(), first.as_mut_ptr()).is_null());
            let client = yolo_noise_new(key().as_ptr(), first.as_mut_ptr());
            let mut out = [0; 32];
            assert_eq!(
                yolo_noise_transform(client, false, first.as_ptr(), 6, out.as_mut_ptr(), 32),
                -1
            );
            yolo_noise_free(client);
        }
    }
}

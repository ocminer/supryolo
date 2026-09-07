// SPDX-License-Identifier: LicenseRef-Supryolo-NC-1.0
//! Local pipe-only test fixture. Public test scalar 1, never a production key.
use std::{
    io::{Read, Write},
    time::Duration,
};
fn main() {
    let public = [
        0x79, 0xbe, 0x66, 0x7e, 0xf9, 0xdc, 0xbb, 0xac, 0x55, 0xa0, 0x62, 0x95, 0xce, 0x87, 0x0b,
        0x07, 0x02, 0x9b, 0xfc, 0xdb, 0x2d, 0xce, 0x28, 0xd9, 0x59, 0xf2, 0x81, 0x5b, 0x16, 0xf8,
        0x17, 0x98,
    ];
    let mut secret = [0; 32];
    secret[31] = 1;
    let mut responder =
        noise_sv2::Responder::from_authority_kp(&public, &secret, Duration::from_secs(60)).unwrap();
    let (mut input, mut output) = (std::io::stdin().lock(), std::io::stdout().lock());
    let mut first = [0; 64];
    input.read_exact(&mut first).unwrap();
    let (response, mut engine) = responder.step_1(first).unwrap();
    output.write_all(&response).unwrap();
    output.flush().unwrap();
    if std::env::args().any(|a| a == "--session") {
        session_fixture(&mut input, &mut output, &mut engine);
        return;
    }
    for _ in 0..5 {
        let mut header = vec![0; 22];
        input.read_exact(&mut header).unwrap();
        engine.decrypt(&mut header).unwrap();
        let n = header[3] as usize + ((header[4] as usize) << 8) + ((header[5] as usize) << 16);
        let mut payload = Vec::new();
        while payload.len() < n {
            let mut chunk = vec![0; (n - payload.len()).min(65519) + 16];
            input.read_exact(&mut chunk).unwrap();
            engine.decrypt(&mut chunk).unwrap();
            payload.extend(chunk);
        }
        engine.encrypt(&mut header).unwrap();
        output.write_all(&header).unwrap();
        for plain in payload.chunks(65519) {
            let mut chunk = plain.to_vec();
            engine.encrypt(&mut chunk).unwrap();
            output.write_all(&chunk).unwrap();
        }
        output.flush().unwrap();
    }
    let mut header = vec![0, 0, 1, 0, 0, 0];
    engine.encrypt(&mut header).unwrap();
    header[0] ^= 1;
    output.write_all(&header).unwrap();
    output.flush().unwrap();
}

fn read_frame(input: &mut impl Read, engine: &mut noise_sv2::NoiseEngine) -> (u8, Vec<u8>) {
    let mut h = vec![0; 22];
    input.read_exact(&mut h).unwrap();
    engine.decrypt(&mut h).unwrap();
    let n = h[3] as usize + ((h[4] as usize) << 8) + ((h[5] as usize) << 16);
    assert!(n < 65520);
    let mut p = vec![0; n + 16];
    input.read_exact(&mut p).unwrap();
    engine.decrypt(&mut p).unwrap();
    (h[2], p)
}
fn send_frame(out: &mut impl Write, engine: &mut noise_sv2::NoiseEngine, ty: u8, mut p: Vec<u8>) {
    let ext: u16 = if ty >= 0x15 { 0x8000 } else { 0 };
    let mut h = ext.to_le_bytes().to_vec();
    h.push(ty);
    h.extend(&(p.len() as u32).to_le_bytes()[..3]);
    engine.encrypt(&mut h).unwrap();
    engine.encrypt(&mut p).unwrap();
    h.extend(p);
    out.write_all(&(h.len() as u32).to_le_bytes()).unwrap();
    out.write_all(&h).unwrap();
    out.flush().unwrap();
}
fn job(id: u32, future: bool) -> Vec<u8> {
    let mut p = 1u32.to_le_bytes().to_vec();
    p.extend(id.to_le_bytes());
    p.push(if future { 0 } else { 1 });
    if !future {
        p.extend(20u32.to_le_bytes());
    }
    p.extend(0xa0000000u32.to_le_bytes());
    p.extend([id as u8; 32]);
    p
}
fn prev(id: u32) -> Vec<u8> {
    let mut p = 1u32.to_le_bytes().to_vec();
    p.extend(id.to_le_bytes());
    p.extend([0; 32]);
    p.extend(10u32.to_le_bytes());
    p.extend((if id == 1 { 0x190f0b50u32 } else { 0x190e0000u32 }).to_le_bytes());
    p
}
fn session_fixture(input: &mut impl Read, out: &mut impl Write, e: &mut noise_sv2::NoiseEngine) {
    assert_eq!(read_frame(input, e).0, 0);
    send_frame(out, e, 1, vec![2, 0, 0, 0, 0, 0]);
    assert_eq!(read_frame(input, e).0, 0x10);
    let mut p = 1u32.to_le_bytes().repeat(2);
    let mut target = [255; 32];
    target[28..].fill(0);
    p.extend(target);
    p.push(4);
    p.extend([1, 2, 3, 4]);
    p.extend([0; 4]);
    send_frame(out, e, 0x11, p);
    send_frame(out, e, 0x15, job(1, true));
    send_frame(out, e, 0x20, prev(1));
    for seq in 10u32..13 {
        let (ty, p) = read_frame(input, e);
        assert_eq!(ty, 0x1a);
        assert_eq!(p.len(), 24);
        assert_eq!(&p[4..8], &seq.to_le_bytes());
    }
    let mut a = 1u32.to_le_bytes().to_vec();
    a.extend(12u32.to_le_bytes());
    a.extend(2u32.to_le_bytes());
    a.extend(2u64.to_le_bytes());
    send_frame(out, e, 0x1c, a);
    let mut err = 1u32.to_le_bytes().to_vec();
    err.extend(11u32.to_le_bytes());
    err.push(11);
    err.extend(b"stale-share");
    send_frame(out, e, 0x1d, err);
    send_frame(out, e, 0x15, job(2, true));
    target[27] = 127;
    let mut t = 1u32.to_le_bytes().to_vec();
    t.extend(target);
    send_frame(out, e, 0x21, t);
    send_frame(out, e, 0x20, prev(2));
    send_frame(out, e, 0x15, job(3, false));
    target[27] = 63;
    let mut t = 1u32.to_le_bytes().to_vec();
    t.extend(target);
    send_frame(out, e, 0x21, t);
}

# Third-party notices

`third_party/nlohmann/json.hpp` is the unmodified single-header distribution of
[nlohmann/json v3.12.0](https://github.com/nlohmann/json/tree/v3.12.0), copyright
Niels Lohmann and contributors, under the MIT License. Its original notices
remain in the header; the full license is alongside it.

OpenSSL, the CUDA toolkit/runtime, compiler runtimes and system threading/socket
libraries are linked dependencies and retain their respective licenses.

The first-party scalar and CUDA BLAKE2b implementations follow
[RFC 7693](https://www.rfc-editor.org/rfc/rfc7693). The repositories discussed in
`docs/ARCHITECTURE.md` were studied as references; their application code is not
vendored here. Future source reuse needs an explicit provenance/license review.

Release packages also carry runtime dependency notices in `third_party/runtime`.
Linux executables statically link OpenSSL (Apache-2.0) and GCC runtime libraries
(GPL with the GCC Runtime Library Exception). CUDA runtime redistribution follows
NVIDIA's toolkit license. Windows packages carry the required MinGW runtime and
OpenSSL DLLs with their original notices. These components are excluded from the
first-party noncommercial restrictions.

SV2 uses the pinned SRI `noise_sv2` library (MIT OR Apache-2.0). Original
notices are in `third_party/sri`; locked transitive dependency notices and
metadata are in `third_party/rust-dependencies`. Rust runtime notices are
in `third_party/rust-runtime`. These libraries retain their original terms.

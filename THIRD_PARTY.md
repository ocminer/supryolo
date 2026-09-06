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

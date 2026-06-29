# PayloadAdapter_N1Legacy

BLUF: reference-only legacy code. It is intentionally not built or wired into
the Neutron 2 deployment.

Use `PayloadAdapter_NeutronSim` as the current copy-me payload adapter pattern.
That adapter implements the active service/adapter contract:

- `PayloadCaptureRequest` in
- `ScienceProductDescriptor` out
- adapter-owned source file path and CRC
- active/async capture behavior

Do not add this directory back to `Components/CMakeLists.txt` unless the legacy
N1 behavior is updated to the current descriptor contract and deliberately
wired into a deployment profile.

# PayloadDriver_N1Legacy

BLUF: reference-only legacy code. It is intentionally not built or wired into
the Neutron 2 deployment.

Use `PayloadDriver_NeutronSim` as the current copy-me payload driver pattern.
That driver implements the active manager/driver contract:

- `PayloadCaptureRequest` in
- `ScienceProductDescriptor` out
- driver-owned source file path and CRC
- active/async capture behavior

Do not add this directory back to `Components/CMakeLists.txt` unless the legacy
N1 behavior is updated to the current descriptor contract and deliberately
wired into a deployment profile.

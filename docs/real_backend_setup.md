# Real Backend Setup (Local Only)

**NEVER COMMIT SDK FILES TO THIS REPOSITORY.**

**Do not commit vendor headers, vendor binaries, or prebuilt SDK libraries.**

This guide is for setting up a real camera SDK on one machine while keeping
this repo clean and shareable.

## Purpose

Use this document when you want to run LabOps against a physical camera backend
on your local machine.

## Safety Rule (Non-Negotiable)

- Keep SDK files outside the repo when possible.
- If you must place SDK files under the repo for local testing, keep them under
  a path matched by `.gitignore` (for example `VendorSDK/`).
- Before commit, verify no SDK files are staged.

## Typical Local Layout

Example (local only):

```text
LabOps Agent/
  VendorSDK/                  # ignored by git
    include/
    lib/
    bin/
```

## Build Flags

Use the real-backend build toggle for local integration work:

```bash
cmake -S . -B tmp/build-real -DLABOPS_ENABLE_REAL_BACKEND=ON
cmake --build tmp/build-real
./tmp/build-real/labops list-backends
./tmp/build-real/labops list-devices --backend real
```

For OSS/local validation (without proprietary SDK enumeration calls), you can
provide a descriptor fixture:

```bash
cat > tmp/devices.csv <<'CSV'
model,serial,user_id,transport,ip,mac
SprintCam,SN-1001,Primary,GigE,10.0.0.21,aa-bb-cc-dd-ee-01
SprintCam,SN-1002,,USB3VISION,,
CSV

export LABOPS_REAL_DEVICE_FIXTURE="$(pwd)/tmp/devices.csv"
./tmp/build-real/labops list-devices --backend real

# Deterministic camera selection in run/baseline flows.
./tmp/build-real/labops run <scenario_with_backend_real_stub.json> --out tmp/runs --device serial:SN-1001
./tmp/build-real/labops baseline capture <scenario_with_backend_real_stub.json> --device user_id:Primary,index:0
```

Selector format supported by `--device` and scenario `device_selector`:

- `serial:<value>`
- `user_id:<value>`
- optional `index:<n>` (0-based tie-break when multiple devices match)

You can point SDK discovery to your local install using either CMake cache vars
or environment variables:

- `VENDOR_SDK_ROOT` (expected to contain `include/` and `lib/` or `lib64/`)
- `VENDOR_SDK_INCLUDE` (explicit include dir override)
- `VENDOR_SDK_LIB` (explicit library dir override)

Examples:

```bash
cmake -S . -B tmp/build-real \
  -DLABOPS_ENABLE_REAL_BACKEND=ON \
  -DVENDOR_SDK_ROOT=/opt/vendor_sdk
```

```bash
export VENDOR_SDK_INCLUDE=/opt/vendor_sdk/include
export VENDOR_SDK_LIB=/opt/vendor_sdk/lib
cmake -S . -B tmp/build-real -DLABOPS_ENABLE_REAL_BACKEND=ON
```

If you are validating default repo behavior (no SDK path):

```bash
cmake -S . -B tmp/build -DLABOPS_ENABLE_REAL_BACKEND=OFF
cmake --build tmp/build
```

## Pre-Commit Safety Check

Run these before every commit during SDK integration work:

```bash
git status --short
```

```bash
git check-ignore -v --stdin <<'PATHS'
VendorSDK/include/vendor_api.h
VendorSDK/lib/vendor_camera.lib
VendorSDK/bin/vendor_camera.dll
VendorSDK/lib/libvendor_camera.so
VendorSDK/lib/libvendor_camera.dylib
PATHS
```

If any SDK file appears as staged/tracked, stop and remove it from the commit.

## Related Docs

- Integration design/implementation guide:
  - `docs/integration/real_sdk_backend.md`

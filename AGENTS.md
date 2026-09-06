# AI Agent Guide - BlueRetro Firmware

## Project overview

This repository contains the ESP32 BlueRetro firmware. This fork adds
controller-specific persistent mapping profiles consumed by WebConfig.

- Production firmware: `main/`
- Queue component: `components/queue_bss/`
- Target configurations: `configs/<hardware>/<system>`
- Integration tests and controller fixtures: `tests/`
- Capture and flashing tools: `tools/`
- Build/test matrix: `.github/workflows/`

The firmware is C built with ESP-IDF. `main/CMakeLists.txt` is the
authoritative source list for the main component: add new implementation
files there when required.

## Code ownership

- `main/adapter/`: adapter state, mapping, macros, GameID, and persistence.
- `main/bluetooth/`: Bluetooth stack, HIDP, GATT/ATT; `att_cfg.c` implements
  configuration operations exposed to WebConfig.
- `main/wired/`: physical protocol implementations for console ports.
- `main/adapter/wired/`: generic controller state translated per target system.
- `main/adapter/wireless/`: parser and normalization per controller family.
- `main/system/`: startup, GPIO, storage, and system management.

Keep console-specific behavior in `main/wired/` or `main/adapter/wired/`.
Do not change generic paths for one console's special case unless the behavior
is intentionally generalized.

## Persistent controller profiles

The implementation is centered in `main/adapter/config.[ch]` and integrated
with `adapter.c`, `bluetooth/att_cfg.c`, and `system/manager.c`.

Preserve these invariants:

- The maximum profile count is `BT_PROFILE_MAX` (currently 7).
- Identity is scoped by wired system, so identical controllers on separate
  systems do not share a profile.
- Classic Bluetooth identity uses its address; BLE identity uses address type
  plus address.
- An existing profile is authoritative for its mapping; GameID remains
  authoritative where applicable.
- Mapping edits are staged in RAM; `commit` writes only the changed profile.
- Current NVS storage is `btprof/p0` through `btprof/p6`; the legacy
  `btprof/profiles` blob must remain readable for compatibility.
- Configuration reset must clear both individual and legacy keys, while the
  independent factory-reset/OTA behavior stays unchanged.

When changing persisted structures, explicitly handle backwards-compatible
reading and migration. Do not reintroduce full-table NVS blob writes: they can
exceed available NVS space.

## Build

Use the validated image `ghcr.io/darthcloud/idf-blueretro:v5.5.0_2024-12-02`.
The `build_hw1_*.sh` scripts prepare the `liblfds7.1.1` dependency and are the
preferred way to generate the targets they cover.

For a manual build in a configured ESP-IDF environment:

```bash
cp configs/hw1/<system> sdkconfig
. "$IDF_PATH/export.sh"
BR_HW=_hw1 BR_SYS=_<system> idf.py reconfigure build
```

Use the matching `configs/dbg/` file for debug/QEMU builds. Do not commit
`sdkconfig`, `build/`, `version.txt`, or generated artifacts.

## Testing

The Python suite is integration testing: it communicates with a running
QEMU/firmware image. Running `pytest` alone is not sufficient unless the
emulator is already running.

The CI-equivalent flow is:

```bash
cp configs/dbg/qemu sdkconfig
. "$IDF_PATH/export.sh"
idf.py build
(cd build; esptool.py --chip esp32 merge_bin --fill-flash-size 4MB -o flash_image.bin @flash_args)
qemu-system-xtensa -machine esp32 -drive file=build/flash_image.bin,if=mtd,format=raw -serial file:serial_log.txt -serial file:gcov_data.gcfn -display none -nic user,model=open_eth,id=lo0,hostfwd=tcp:127.0.0.1:8001-:80 -daemonize
pytest
```

For profile work, cover new identity creation, reconnect, system scoping,
selective commit, reboot persistence, and configuration reset. For a parser or
mapping change, run the relevant `tests/pytest_*.py` controller tests.

## Change conventions

- Follow local C style: 4-space indentation, opening braces on the declaration
  line, and short English comments.
- Use explicit-width types (`uint8_t`, `int32_t`, and so on). Use `__packed`
  when protocol or persisted layout matters.
- Update headers alongside implementations; keep `CMakeLists.txt` in sync.
- Do not make broad formatting-only or unrelated changes.
- Never modify `components/queue_bss/liblfds/`; it is an external dependency
  restored by the build environment.
- Preserve pre-existing worktree edits. Do not revert or accidentally include
  them in a task's change.

## Before handing off a change

1. Review the diff and ensure it contains only the requested work.
2. Run the smallest relevant build and tests feasible in the environment.
3. State the configuration used and any validation that could not run.

<p align="center"><a href="https://blueretro.com/products/brx"><img src="https://github.com/darthcloud/img/blob/main/brx_banner.png"/></a></p>

# BlueRetro

BlueRetro is a Bluetooth controller adapter for retro game consoles and computers.

This repository is a fork of the upstream BlueRetro firmware with controller-specific persistent mapping profiles and the corresponding WebConfig support.

## Fork changes

### Persistent controller profiles

The firmware stores controller-specific mappings in ESP32 NVS.

- Up to 7 profiles.
- Classic Bluetooth identity by Bluetooth address.
- BLE identity by address type plus Bluetooth address.
- Profiles scoped by wired console/system ID.
- Profile identity is based on controller MAC address + system ID, preventing duplicate profiles within the same system while allowing separate profiles for different systems.
- Existing controller profile mapping is authoritative.
- A new controller is automatically registered when no matching profile exists.
- Different controllers can share the same runtime slot without overwriting each other's stored mapping.
- GameID configuration remains authoritative.

### Profile lifecycle

The profile flow is:

1. Resolve controller identity.
2. Search for a matching profile.
3. Create one when none exists.
4. Persist controller metadata.
5. Apply the stored profile mapping.
6. Stage mapping edits in RAM.
7. Commit only the affected profile.

### NVS persistence

The original whole-table persistence attempted to write the complete profile table as one large NVS blob. That path could fail with `ESP_ERR_NVS_NOT_ENOUGH_SPACE`.

This fork stores individual profile records using:

```text
btprof/p0
btprof/p1
btprof/p2
btprof/p3
btprof/p4
btprof/p5
btprof/p6
```

Only the affected profile is rewritten when a mapping is committed.

The legacy `profiles` blob remains readable for compatibility.

### Custom preset persistence

Custom presets are stored globally in the ESP32 SPIFFS filesystem, independently from the controller profile records stored in NVS.

The database is maintained at:

```text
/fs/custom-presets.json
```

Temporary and backup files are used during transactional updates:

```text
/fs/custom-presets.tmp
/fs/custom-presets.bak
```

The firmware validates the temporary database before promoting it to the active file, protecting the stored preset database from incomplete writes.

Custom preset data is exposed to the WebConfig through the firmware GATT interface using commands for metadata retrieval, chunked data transfer and commit.

### Configuration reset

Configuration reset clears both profile storage formats:

- `p0` through `p6`
- legacy `profiles`

Custom presets are preserved during the normal configuration reset.

The normal firmware remains installed. The original full factory-reset/OTA behavior remains separate.

## Public WebConfig

The WebConfig interface for this firmware fork is available at:
[BlueRetro Controller-Specific Profiles WebConfig](https://welintron.github.io/webconfig/)


Use the WebConfig together with the latest BlueRetro Controller-Specific Profiles WebConfig release.

The WebConfig provides the interface for selecting, reading, modifying and committing persistent controller-specific mappings and for managing custom presets stored by the firmware.

## Release

**Firmware version: v1.3.0**

This release adds persistent custom preset storage in SPIFFS while retaining controller-specific profile persistence in NVS. It also includes the Flydigi Vader 5 Pro support and the controller profile identity fix introduced in the previous release.

### Custom presets

The firmware provides the storage and GATT transport used by WebConfig to manage custom presets.

The custom preset database supports:

- metadata retrieval with database version, length and checksum;
- chunked database reads;
- chunked database writes;
- validation and transactional commit;
- recovery using the active, temporary and backup database files.

Custom presets are stored globally and are not duplicated per controller profile.

### Flydigi Vader 5 Pro and Victrix Pro BFG Reloaded support

The Flydigi Vader 5 Pro and Victrix Pro BFG Reloaded are supported through the Bluetooth/HID handling added for the controller.

### Profile identity fix

Profile identity is based on:

- Controller MAC address.
- Wired console/system ID.

This allows the same physical controller to have different profiles for different systems while preventing duplicate profiles within the same system.

### Tested targets

The v1.3.0 firmware includes the previously validated targets:

- 3DO
- Nintendo 64
- Neo Geo
- PlayStation
- Sega Saturn

### Tested controllers

- 8BitDo S30
- Sony DualSense
- Victrix Pro BFG Reloaded Xbox
- Flydigi Vader 5 Pro

For installation, use the firmware binary corresponding to the hardware and system target of your BlueRetro adapter.

For the complete source code and build instructions, see this repository.

### WebConfig GATT interface

The firmware exposes profile operations used by the WebConfig:

- profile list/count
- profile record/identity
- mapping read
- mapping write
- mapping commit

It also exposes custom preset operations for:

- custom preset metadata
- custom preset data reads
- custom preset data writes
- custom preset database commit

### Tested hardware targets

The fork was physically validated with the same profile firmware on:

| Target | Configuration |
|---|---|
| 3DO | `configs/hw1/3do` |
| PlayStation | `configs/hw1/playstation` |
| Sega Saturn | `configs/hw1/saturn` |
| Nintendo 64 | `configs/hw1/n64` |
| Neo Geo | `configs/hw1/parallel_1p` |

The tests used 8BitDo S30 and DualSense controllers and covered normal operation, custom mappings, disconnect/reconnect, power-cycle persistence and configuration reset.

## Build

Validated Docker image:

```text
ghcr.io/darthcloud/idf-blueretro:v5.5.0_2024-12-02
```

For an HW1 target:

```bash
cp configs/hw1/<target> sdkconfig
. "$IDF_PATH/export.sh"

export BR_HW=_hw1
export BR_SYS=_<system>

idf.py build
```

The build environment also restores the `liblfds7.1.1` dependency used by `queue_bss`.

## Relevant source areas

The profile implementation is concentrated in:

```text
main/adapter/config.c
main/adapter/config.h
main/adapter/adapter.c
main/bluetooth/att_cfg.c
main/system/manager.c
```

The console-specific transport implementations are unchanged by the profile persistence work.

## Upstream

This project is based on DarthCloud's BlueRetro:

https://github.com/darthcloud/BlueRetro

See the upstream repository for the original project documentation, hardware information and licensing terms.

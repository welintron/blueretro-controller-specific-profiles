# Fork changes

This fork adds persistent controller-specific profiles to BlueRetro and the
GATT interface required by WebConfig.

Firmware changes:
- persistent controller profiles in ESP32 NVS;
- Bluetooth/BLE identity matching;
- wired-system scoping;
- automatic profile creation;
- per-profile persistence;
- legacy profile-blob compatibility;
- complete profile clearing on configuration reset;
- profile list, identity, mapping read/write and commit GATT operations.

The profile layer was validated on:
- 3DO
- PlayStation
- Sega Saturn
- Nintendo 64
- Neo Geo (parallel_1p)

The implementation was tested with 8BitDo S30 and DualSense controllers.

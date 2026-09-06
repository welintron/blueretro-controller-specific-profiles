# Custom Presets BLE protocol – firmware etapa 2

Uses the existing `BR_CFG_CMD_CHRC_HDL` characteristic.

Command IDs:

- `0x27 GET_CUSTOM_PRESETS`
- `0x28 GET_CUSTOM_PRESETS_DATA`
- `0x29 SET_CUSTOM_PRESETS_DATA`
- `0x2A COMMIT_CUSTOM_PRESETS`

## GET_CUSTOM_PRESETS

Write: `[0x27]`

Then read `BR_CFG_CMD_CHRC_HDL`.

Response (9 bytes):

- byte 0: JSON format version (`uint8_t`)
- bytes 1..4: JSON length (`uint32_t`, little-endian)
- bytes 5..8: CRC32 (`uint32_t`, little-endian)

## GET_CUSTOM_PRESETS_DATA

Write: `[0x28][offset u32 LE]`

Then read `BR_CFG_CMD_CHRC_HDL`.

The response contains up to `ATT MTU - 1` bytes from the JSON at the requested offset. Non-zero offsets use `READ_BLOB_RSP`.

## SET_CUSTOM_PRESETS_DATA

Each write has:

`[0x29][total_len u32 LE][offset u32 LE][data...]`

The first packet must use offset `0`. It starts a new transactional upload to `/fs/custom-presets.tmp`. Subsequent packets must use the exact next offset. The active `/fs/custom-presets.json` is not replaced until commit succeeds.

## COMMIT_CUSTOM_PRESETS

Write: `[0x2A]`

The firmware flushes and closes the temporary file, validates JSON version/structure/length/checksum, and atomically promotes it to the active database with backup recovery.

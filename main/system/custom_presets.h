/*
 * Copyright (c) 2026, Welintron
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef _CUSTOM_PRESETS_H_
#define _CUSTOM_PRESETS_H_

#include <stddef.h>
#include <stdint.h>

#define CUSTOM_PRESETS_VERSION 1
#define CUSTOM_PRESETS_MAX_SIZE (128 * 1024)
#define CUSTOM_PRESETS_BACKUP_FILE "/fs/custom-presets.bak"

/*
 * Custom Presets are stored as one versioned JSON document in SPIFFS.
 *
 * The checksum is CRC32 (little-endian) of the complete JSON document with
 * the eight hexadecimal characters of the "checksum" value replaced by
 * eight ASCII zeroes.
 */

int custom_presets_init(void);

int custom_presets_get_size(uint32_t *size);
int custom_presets_get_checksum(uint32_t *checksum);
int custom_presets_read(uint32_t offset, void *data, uint32_t len);

/*
 * Start receiving a new complete database into CUSTOM_PRESETS_TMP_FILE.
 * The active database is left untouched until custom_presets_commit() succeeds.
 */
int custom_presets_write_begin(uint32_t total_len);
int custom_presets_write(uint32_t offset, const void *data, uint32_t len);
int custom_presets_commit(void);
void custom_presets_write_abort(void);

#endif /* _CUSTOM_PRESETS_H_ */

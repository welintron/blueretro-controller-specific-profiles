/*
 * Copyright (c) 2026, Welintron
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <inttypes.h>

#include <esp32/rom/crc.h>
#include "cJSON.h"

#include "custom_presets.h"
#include "fs.h"

#define CUSTOM_PRESETS_CHECKSUM_HEX_LEN 8
#define CUSTOM_PRESETS_CHECKSUM_KEY "\"checksum\""

static FILE *write_file;
static uint32_t write_len;
static uint32_t write_expected_len;

static int custom_presets_read_file(const char *path, char **buffer, size_t *len) {
    FILE *file;
    struct stat st;
    size_t read_len;

    if (stat(path, &st) != 0 || st.st_size <= 0 ||
        st.st_size > CUSTOM_PRESETS_MAX_SIZE) {
        return -1;
    }

    file = fopen(path, "rb");
    if (!file) {
        return -1;
    }

    *buffer = malloc((size_t)st.st_size + 1);
    if (!*buffer) {
        fclose(file);
        return -1;
    }

    read_len = fread(*buffer, 1, (size_t)st.st_size, file);
    fclose(file);

    if (read_len != (size_t)st.st_size) {
        free(*buffer);
        *buffer = NULL;
        return -1;
    }

    (*buffer)[read_len] = '\0';
    *len = read_len;
    return 0;
}

static int custom_presets_find_checksum(char *buffer, char **checksum) {
    char *key;
    char *value;

    key = strstr(buffer, CUSTOM_PRESETS_CHECKSUM_KEY);
    if (!key) {
        return -1;
    }

    value = strchr(key + strlen(CUSTOM_PRESETS_CHECKSUM_KEY), ':');
    if (!value) {
        return -1;
    }

    value++;
    while (*value == ' ' || *value == '\t' || *value == '\r' || *value == '\n') {
        value++;
    }

    if (*value != '"') {
        return -1;
    }

    value++;
    if (strlen(value) < CUSTOM_PRESETS_CHECKSUM_HEX_LEN + 1 ||
        value[CUSTOM_PRESETS_CHECKSUM_HEX_LEN] != '"') {
        return -1;
    }

    *checksum = value;
    return 0;
}

static int custom_presets_parse_checksum(const char *value, uint32_t *checksum) {
    unsigned int parsed;

    for (size_t i = 0; i < CUSTOM_PRESETS_CHECKSUM_HEX_LEN; i++) {
        char c = value[i];
        if (!((c >= '0' && c <= '9') ||
              (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F'))) {
            return -1;
        }
    }

    if (sscanf(value, "%8x", &parsed) != 1) {
        return -1;
    }

    *checksum = (uint32_t)parsed;
    return 0;
}

static uint32_t custom_presets_calculate_checksum(char *buffer) {
    char *checksum;
    char saved[CUSTOM_PRESETS_CHECKSUM_HEX_LEN];
    uint32_t crc;

    if (custom_presets_find_checksum(buffer, &checksum)) {
        return 0;
    }

    memcpy(saved, checksum, sizeof(saved));
    memset(checksum, '0', sizeof(saved));

    crc = crc32_le(0, (const uint8_t *)buffer, strlen(buffer));

    memcpy(checksum, saved, sizeof(saved));
    return crc;
}

static int custom_presets_validate(const char *path, uint32_t expected_len,
    uint32_t *checksum_out) {
    char *buffer = NULL;
    char *checksum_value;
    size_t len;
    uint32_t stored_checksum;
    uint32_t calculated_checksum;
    cJSON *root = NULL;
    cJSON *version;
    cJSON *presets;
    int ret = -1;

    if (custom_presets_read_file(path, &buffer, &len)) {
        return -1;
    }

    if (expected_len && len != expected_len) {
        goto out;
    }

    root = cJSON_ParseWithLength(buffer, len);
    if (!root || !cJSON_IsObject(root)) {
        goto out;
    }

    version = cJSON_GetObjectItemCaseSensitive(root, "version");
    presets = cJSON_GetObjectItemCaseSensitive(root, "presets");

    if (!cJSON_IsNumber(version) || version->valueint != CUSTOM_PRESETS_VERSION ||
        !cJSON_IsArray(presets)) {
        goto out;
    }

    if (custom_presets_find_checksum(buffer, &checksum_value)) {
        goto out;
    }

    if (custom_presets_parse_checksum(checksum_value, &stored_checksum)) {
        goto out;
    }

    calculated_checksum = custom_presets_calculate_checksum(buffer);
    if (stored_checksum != calculated_checksum) {
        goto out;
    }

    if (checksum_out) {
        *checksum_out = stored_checksum;
    }

    ret = 0;

out:
    cJSON_Delete(root);
    free(buffer);
    return ret;
}

static int custom_presets_write_default(void) {
    static const char template[] =
        "{\"version\":1,\"checksum\":\"00000000\",\"presets\":[]}";
    char buffer[sizeof(template)];
    char *checksum;
    uint32_t crc;
    FILE *file;

    memcpy(buffer, template, sizeof(template));

    if (custom_presets_find_checksum(buffer, &checksum)) {
        return -1;
    }

    crc = custom_presets_calculate_checksum(buffer);
    char checksum_text[CUSTOM_PRESETS_CHECKSUM_HEX_LEN + 1];
    if (snprintf(checksum_text, sizeof(checksum_text), "%08" PRIX32, crc) !=
        CUSTOM_PRESETS_CHECKSUM_HEX_LEN) {
        return -1;
    }
    memcpy(checksum, checksum_text, CUSTOM_PRESETS_CHECKSUM_HEX_LEN);

    file = fopen(CUSTOM_PRESETS_TMP_FILE, "wb");
    if (!file) {
        return -1;
    }

    if (fwrite(buffer, 1, sizeof(buffer) - 1, file) != sizeof(buffer) - 1 ||
        fflush(file) != 0) {
        fclose(file);
        remove(CUSTOM_PRESETS_TMP_FILE);
        return -1;
    }

    if (fclose(file) != 0) {
        remove(CUSTOM_PRESETS_TMP_FILE);
        return -1;
    }

    if (rename(CUSTOM_PRESETS_TMP_FILE, CUSTOM_PRESETS_FILE) != 0) {
        remove(CUSTOM_PRESETS_TMP_FILE);
        return -1;
    }

    return 0;
}

static int custom_presets_recover(void) {
    uint32_t checksum;

    if (custom_presets_validate(CUSTOM_PRESETS_FILE, 0, &checksum) == 0) {
        remove(CUSTOM_PRESETS_TMP_FILE);
        remove(CUSTOM_PRESETS_BACKUP_FILE);
        return 0;
    }

    if (custom_presets_validate(CUSTOM_PRESETS_TMP_FILE, 0, &checksum) == 0) {
        remove(CUSTOM_PRESETS_FILE);
        return rename(CUSTOM_PRESETS_TMP_FILE, CUSTOM_PRESETS_FILE);
    }

    if (custom_presets_validate(CUSTOM_PRESETS_BACKUP_FILE, 0, &checksum) == 0) {
        remove(CUSTOM_PRESETS_FILE);
        return rename(CUSTOM_PRESETS_BACKUP_FILE, CUSTOM_PRESETS_FILE);
    }

    remove(CUSTOM_PRESETS_TMP_FILE);
    remove(CUSTOM_PRESETS_BACKUP_FILE);
    remove(CUSTOM_PRESETS_FILE);
    return custom_presets_write_default();
}

int custom_presets_init(void) {
    return custom_presets_recover();
}

int custom_presets_get_size(uint32_t *size) {
    struct stat st;

    if (!size || stat(CUSTOM_PRESETS_FILE, &st) != 0 ||
        st.st_size <= 0 || st.st_size > CUSTOM_PRESETS_MAX_SIZE) {
        return -1;
    }

    *size = (uint32_t)st.st_size;
    return 0;
}

int custom_presets_get_checksum(uint32_t *checksum) {
    if (!checksum) {
        return -1;
    }

    return custom_presets_validate(CUSTOM_PRESETS_FILE, 0, checksum);
}

int custom_presets_read(uint32_t offset, void *data, uint32_t len) {
    FILE *file;
    size_t read_len;

    if (!data || !len) {
        return -1;
    }

    file = fopen(CUSTOM_PRESETS_FILE, "rb");
    if (!file) {
        return -1;
    }

    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }

    read_len = fread(data, 1, len, file);
    fclose(file);

    return (int)read_len;
}

int custom_presets_write_begin(uint32_t total_len) {
    if (!total_len || total_len > CUSTOM_PRESETS_MAX_SIZE || write_file) {
        return -1;
    }

    remove(CUSTOM_PRESETS_TMP_FILE);

    write_file = fopen(CUSTOM_PRESETS_TMP_FILE, "wb");
    if (!write_file) {
        return -1;
    }

    write_len = 0;
    write_expected_len = total_len;
    return 0;
}

int custom_presets_write(uint32_t offset, const void *data, uint32_t len) {
    size_t written;

    if (!write_file || !data || !len ||
        offset != write_len ||
        write_len + len > write_expected_len) {
        return -1;
    }

    written = fwrite(data, 1, len, write_file);
    if (written != len) {
        return -1;
    }

    write_len += len;
    return 0;
}

int custom_presets_commit(void) {
    uint32_t checksum;

    if (!write_file || write_len != write_expected_len) {
        return -1;
    }

    if (fflush(write_file) != 0) {
        fclose(write_file);
        write_file = NULL;
        remove(CUSTOM_PRESETS_TMP_FILE);
        return -1;
    }

    if (fclose(write_file) != 0) {
        write_file = NULL;
        remove(CUSTOM_PRESETS_TMP_FILE);
        return -1;
    }

    write_file = NULL;

    if (custom_presets_validate(CUSTOM_PRESETS_TMP_FILE, write_expected_len, &checksum)) {
        remove(CUSTOM_PRESETS_TMP_FILE);
        return -1;
    }

    /*
     * Keep a recoverable copy of the active database until the new database
     * has been promoted successfully.
     */
    remove(CUSTOM_PRESETS_BACKUP_FILE);
    if (rename(CUSTOM_PRESETS_FILE, CUSTOM_PRESETS_BACKUP_FILE) != 0) {
        struct stat st;
        if (stat(CUSTOM_PRESETS_FILE, &st) == 0) {
            remove(CUSTOM_PRESETS_TMP_FILE);
            return -1;
        }
    }

    if (rename(CUSTOM_PRESETS_TMP_FILE, CUSTOM_PRESETS_FILE) != 0) {
        /*
         * Restore the previous database if promotion failed.
         */
        remove(CUSTOM_PRESETS_FILE);
        rename(CUSTOM_PRESETS_BACKUP_FILE, CUSTOM_PRESETS_FILE);
        return -1;
    }

    remove(CUSTOM_PRESETS_BACKUP_FILE);
    write_len = 0;
    write_expected_len = 0;
    return 0;
}

void custom_presets_write_abort(void) {
    if (write_file) {
        fclose(write_file);
        write_file = NULL;
    }

    remove(CUSTOM_PRESETS_TMP_FILE);
    write_len = 0;
    write_expected_len = 0;
}

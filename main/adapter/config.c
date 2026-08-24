/*
 * Copyright (c) 2019-2025, Jacques Gagnon
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include "nvs.h"
#include "zephyr/types.h"
#include "tools/util.h"
#include "adapter.h"
#include "config.h"
#include "system/fs.h"
#include "adapter/gameid.h"
#include "bluetooth/mon.h"
#include "bluetooth/host.h"
#include "system/manager.h"

struct config config;
struct hw_config hw_config = {
    .external_adapter = 0,
    .hotplug = 0,
    .hw1_ports_led_pins = {2, 4, 12, 15},
    .led_flash_duty_cycle = 0x80000,
    .led_flash_hz = {2, 4, 8},
    .led_flash_off_duty_cycle = 0,
    .led_flash_on_duty_cycle = 0xFFFFF,
    .led_pulse_duty_max = 2000,
    .led_pulse_duty_min = 50,
    .led_pulse_fade_cycle_delay_ms = 500,
    .led_pulse_fade_time_ms = 500,
    .led_pulse_hz = 5000,
    .led_pulse_off_duty_cycle = 0,
    .led_pulse_on_duty_cycle = 0x1FFF,
    .port_cnt = 2,
    .ports_sense_input_polarity = 0,
    .ports_sense_output_ms = 1000,
    .ports_sense_output_od = 0,
    .ports_sense_output_polarity = 0,
    .ports_sense_p3_p4_as_output = 0,
    .power_pin_is_hold = 0,
    .power_pin_od = 0,
    .power_pin_polarity = 0,
    .power_pin_pulse_ms = 20,
    .reset_pin_od = 1,
    .reset_pin_polarity = 0,
    .reset_pin_pulse_ms = 500,
    .sw_io0_hold_thres_ms = {1000, 3000, 6000},
    .ps_ctrl_colors = {
        0xFF0000, /* Blue */
        0x0000FF, /* Red */
        0x00FF00, /* Green */
        0xFF00FF, /* Pink */
        0xFFFF00, /* Cyan */
        0x0080FF, /* Orange */
        0x00FFFF, /* Yellow */
        0xFF0080, /* Purple */
    },
};

static char *hw_config_name_idx[] = {
    "ext_adapter",
    "hotplug",
    "hw1_led_pins_0",
    "hw1_led_pins_1",
    "hw1_led_pins_2",
    "hw1_led_pins_3",
    "led_flash_duty",
    "led_flash_hz_0",
    "led_flash_hz_1",
    "led_flash_hz_2",
    "led_f_off_duty",
    "led_f_on_duty",
    "led_p_duty_max",
    "led_p_duty_min",
    "led_p_fade_c_ms",
    "led_p_fade_t_ms",
    "led_pulse_hz",
    "led_p_off_duty",
    "led_p_on_duty",
    "port_cnt",
    "ports_s_in_pol",
    "ports_s_out_ms",
    "ports_s_out_od",
    "ports_s_out_pol",
    "ports_s_out_en",
    "pwr_pin_is_hold",
    "pwr_pin_od",
    "pwr_pin_pol",
    "pwr_pin_p_ms",
    "reset_pin_od",
    "reset_pin_pol",
    "reset_pin_p_ms",
    "sw_thres_ms_0",
    "sw_thres_ms_1",
    "sw_thres_ms_2",
    "ps_ctrl_color_0",
    "ps_ctrl_color_1",
    "ps_ctrl_color_2",
    "ps_ctrl_color_3",
    "ps_ctrl_color_4",
    "ps_ctrl_color_5",
    "ps_ctrl_color_6",
    "ps_ctrl_color_7",
};

#define BT_PROFILE_NVS_NS "btprof"
#define BT_PROFILE_NVS_KEY "profiles"
#define BT_PROFILE_NVS_PROFILE_KEY_FMT "p%u"
#define BT_PROFILE_ADDR_CLASSIC 0xFF

struct bt_profile_v4 {
    uint8_t valid;
    uint8_t addr_type;
    uint8_t system_id;
    uint8_t source_out_idx;
    uint8_t bdaddr[6];
    struct in_cfg in_cfg;
} __packed;

struct bt_profile_v11 {
    uint8_t valid;
    uint8_t addr_type;
    uint8_t system_id;
    uint8_t source_out_idx;
    uint8_t bdaddr[6];
    char name[32];
    struct in_cfg in_cfg;
} __packed;

static struct bt_profile bt_profiles[BT_PROFILE_MAX];
static bool bt_profiles_loaded = false;

static bool bt_profile_addr_equal(const struct bt_profile *profile,
                                  uint8_t addr_type,
                                  uint8_t system_id,
                                  const uint8_t *bdaddr)
{
    return profile->valid &&
           profile->addr_type == addr_type &&
           profile->system_id == system_id &&
           memcmp(profile->bdaddr, bdaddr, sizeof(profile->bdaddr)) == 0;
}

static uint8_t bt_profile_source_id_from_name(const char *name)
{
    if (!name) {
        return 0;
    }

    /*
     * Source IDs are Web Config labelName indexes:
     *   3  = PS3
     *   4  = PS4 / PS5
     *   22 = NeoGeo (Parallel 1P)
     *   34 = N64
     *   38 = Saturn
     *   40 = PSX / PS2
     */

    /* Keep specific modern PlayStation devices distinct from PSX/PS2. */
    if (strstr(name, "DualSense") ||
        strstr(name, "Wireless Controller")) {
        return 4; /* PS4 / PS5 */
    }

    if (strstr(name, "PLAYSTATION(R)3") ||
        strstr(name, "PS3")) {
        return 3; /* PS3 */
    }

    /* Explicit PS1/PS2-style Bluetooth/generic HID names. */
    if (strstr(name, "8BitDo PS1 Modkit") || strstr(name, "8BitDo P30 Modkit") || strstr(name, "8BitDo P30 classic Modkit") || strstr(name, "8BitDo P30 Classic Modkit") || strstr(name, "PS1") ||
        strstr(name, "PS One") ||
        strstr(name, "PSX") ||
        strstr(name, "PS2") ||
        strstr(name, "PlayStation 1") ||
        strstr(name, "PlayStation 2") ||
        strstr(name, "PlayStation Controller")) {
        return 40; /* PSX / PS2 */
    }

    if (strstr(name, "NEOGEO") ||
        strstr(name, "NeoGeo") ||
        strstr(name, "Neo Geo") ||
        strstr(name, "8BitDo NEOGEO GP")) {
        return 22; /* NeoGeo (Parallel 1P) */
    }

    if (strstr(name, "S30 Modkit") ||
        strstr(name, "M30 Modkit") ||
        strstr(name, "M30 gamepad") ||
        strstr(name, "Saturn Controller") ||
        strstr(name, "Saturn Pad")) {
        return 38; /* Saturn */
    }

    if (strstr(name, "N64") ||
        strstr(name, "Brawler64") ||
        strstr(name, "Nintendo 64")) {
        return 34; /* N64 */
    }

    return 0;
}

static uint8_t bt_profile_source_id(struct bt_dev *device)
{
    if (!device) {
        return 0;
    }

    /*
     * Prefer BlueRetro's actual controller identification over the
     * advertised Bluetooth name.
     */
    if (device->ids.id >= 0 && device->ids.id < BT_MAX_DEV) {
        atomic_t *pad_flags =
            &bt_adapter.data[device->ids.id].base.flags[PAD];

        if (atomic_test_bit(pad_flags, BT_QUIRK_8BITDO_SATURN)) {
            return 38; /* Saturn */
        }

        if (atomic_test_bit(pad_flags, BT_QUIRK_8BITDO_N64) ||
            atomic_test_bit(pad_flags, BT_QUIRK_8BITDO_N64_MK) ||
            atomic_test_bit(pad_flags, BT_QUIRK_BLUEN64_N64)) {
            return 34; /* N64 */
        }
    }

    if (device->ids.type == BT_PS) {
        return 4; /* PS4 / PS5 */
    }

    if (device->ids.type == BT_PS3) {
        return 3; /* PS3 */
    }

    if (device->ids.type == BT_SW &&
        device->ids.subtype == BT_SW_N64) {
        return 34; /* N64 */
    }

    /*
     * Generic HID devices such as the 8BitDo NEOGEO GP and many PS1/PS2
     * controllers are resolved by their advertised name as a fallback.
     */
    if (device->name && device->name->name[0]) {
        return bt_profile_source_id_from_name(device->name->name);
    }

    return 0;
}

static int32_t bt_profile_find(uint8_t addr_type,
                               uint8_t system_id,
                               const uint8_t *bdaddr)
{
    for (uint32_t i = 0; i < BT_PROFILE_MAX; i++) {
        if (bt_profile_addr_equal(&bt_profiles[i], addr_type,
                                      system_id, bdaddr)) {
            return i;
        }
    }
    return -1;
}

static int32_t bt_profile_alloc(void)
{
    for (uint32_t i = 0; i < BT_PROFILE_MAX; i++) {
        if (!bt_profiles[i].valid) {
            return i;
        }
    }
    return -1;
}

static int32_t bt_profile_get_addr(struct bt_dev *device,
                                   uint8_t *addr_type,
                                   uint8_t *bdaddr)
{
    if (!device || !addr_type || !bdaddr) {
        return -1;
    }

    if (atomic_test_bit(&device->flags, BT_DEV_IS_BLE)) {
        *addr_type = device->le_remote_bdaddr.type;
        memcpy(bdaddr, device->le_remote_bdaddr.a.val, 6);
    }
    else {
        *addr_type = BT_PROFILE_ADDR_CLASSIC;
        memcpy(bdaddr, device->remote_bdaddr, 6);
    }

    return 0;
}


/* V30 profile lifecycle trace */
enum { V30_NVS_LOAD=1, V30_SAVE=2, V30_ENSURE=3, V30_APPLY=4,
       V30_IDENTITY=5, V30_SYNC=6, V30_CLEAR=7, V30_WRITE=8,
       V30_COMMIT=9, V30_CONFIG_INIT=10, V30_CONFIG_UPDATE=11 };
struct bt_profile_v30_event {
    uint32_t seq; uint8_t type; int8_t profile; uint8_t source; uint8_t out;
    int32_t result; uint32_t before_hash; uint32_t after_hash;
    uint32_t table_hash; uint32_t active_hash; uint32_t aux;
} __packed;
struct bt_profile_v30_snapshot {
    int32_t open_err; int32_t get_err; uint32_t stored_size; uint32_t expected_size;
    uint32_t loaded_blob_hash; uint32_t table_hash; uint32_t profile0_hash;
    uint32_t runtime_hash; uint8_t source; uint8_t profile0_valid;
    uint8_t profile0_map_size; uint8_t profile_count; uint32_t last_seq;
} __packed;
static struct bt_profile_v30_event bt_profile_v30_last;
static struct bt_profile_v30_snapshot bt_profile_v30_snap;
static uint32_t bt_profile_v30_seq;
static uint32_t bt_profile_v30_cfg_hash(const struct in_cfg *cfg){
    uint32_t h=2166136261u, n=cfg?cfg->map_size:0; if(n>ADAPTER_MAPPING_MAX)n=ADAPTER_MAPPING_MAX;
    const uint8_t *p=cfg?(const uint8_t*)cfg->map_cfg:NULL;
    for(uint32_t i=0;p&&i<n*sizeof(struct map_cfg);i++){h^=p[i];h*=16777619u;} return h;
}
static uint32_t bt_profile_v30_table_hash(void){
    uint32_t h=2166136261u; const uint8_t *p=(const uint8_t*)bt_profiles;
    for(uint32_t i=0;i<sizeof(bt_profiles);i++){h^=p[i];h*=16777619u;} return h;
}
static uint8_t bt_profile_v30_count(void){uint8_t n=0;for(uint32_t i=0;i<BT_PROFILE_MAX;i++)if(bt_profiles[i].valid)n++;return n;}
static void bt_profile_v30_trace(uint8_t t,int8_t p,uint8_t out,int32_t r,uint32_t b,uint32_t af,uint32_t aux){
    ++bt_profile_v30_seq; bt_profile_v30_last.seq=bt_profile_v30_seq; bt_profile_v30_last.type=t;
    bt_profile_v30_last.profile=p; bt_profile_v30_last.source=(uint8_t)config_get_src(); bt_profile_v30_last.out=out;
    bt_profile_v30_last.result=r; bt_profile_v30_last.before_hash=b; bt_profile_v30_last.after_hash=af;
    bt_profile_v30_last.table_hash=bt_profile_v30_table_hash();
    bt_profile_v30_last.active_hash=(out<WIRED_MAX_DEV)?bt_profile_v30_cfg_hash(&config.in_cfg[out]):0;
    bt_profile_v30_last.aux=aux;
}
static void bt_profile_v30_snap_update(void){
    bt_profile_v30_snap.table_hash=bt_profile_v30_table_hash();
    bt_profile_v30_snap.profile0_valid=bt_profiles[0].valid?1:0;
    bt_profile_v30_snap.profile0_map_size=bt_profiles[0].valid?bt_profiles[0].in_cfg.map_size:0;
    bt_profile_v30_snap.profile0_hash=bt_profiles[0].valid?bt_profile_v30_cfg_hash(&bt_profiles[0].in_cfg):0;
    bt_profile_v30_snap.runtime_hash=bt_profile_v30_cfg_hash(&config.in_cfg[0]);
    bt_profile_v30_snap.source=(uint8_t)config_get_src(); bt_profile_v30_snap.profile_count=bt_profile_v30_count();
    bt_profile_v30_snap.last_seq=bt_profile_v30_seq;
}
uint32_t config_bt_profile_v30_event_diag(uint8_t *d,uint32_t m){uint32_t n=sizeof(bt_profile_v30_last);if(!d||m<n)return 0;memcpy(d,&bt_profile_v30_last,n);return n;}
uint32_t config_bt_profile_v30_snapshot_diag(uint8_t *d,uint32_t m){uint32_t n=sizeof(bt_profile_v30_snap);if(!d||m<n)return 0;bt_profile_v30_snap_update();memcpy(d,&bt_profile_v30_snap,n);return n;}


struct bt_profile_v31_diag {
    uint8_t kind;
    uint8_t event_type;
    int8_t profile_index;
    uint8_t config_source;
    uint32_t seq;
    int32_t result;
    uint32_t hash_before;
    uint32_t hash_after;
} __packed;

static struct bt_profile_v31_diag bt_profile_v31_last;

struct bt_profile_v31_state {
    int32_t nvs_open_err;
    int32_t nvs_get_err;
    uint32_t stored_size;
    uint32_t expected_size;
    uint32_t loaded_hash;
    uint32_t table_hash;
    uint32_t profile0_hash;
    uint32_t runtime_hash;
    uint32_t seq;
    uint8_t profile0_valid;
    uint8_t profile0_map_size;
    uint8_t profile_count;
    uint8_t config_source;
} __packed;

static struct bt_profile_v31_state bt_profile_v31_state_last;

static uint32_t bt_profile_v31_cfg_hash(const struct in_cfg *cfg)
{
    uint32_t hash = 2166136261u;
    uint32_t map_size = cfg ? cfg->map_size : 0;
    if (map_size > ADAPTER_MAPPING_MAX) {
        map_size = ADAPTER_MAPPING_MAX;
    }

    const uint8_t *bytes = cfg ? (const uint8_t *)cfg->map_cfg : NULL;
    for (uint32_t i = 0; bytes && i < map_size * sizeof(struct map_cfg); i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t bt_profile_v31_table_hash(void)
{
    uint32_t hash = 2166136261u;
    const uint8_t *bytes = (const uint8_t *)bt_profiles;
    for (uint32_t i = 0; i < sizeof(bt_profiles); i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint8_t bt_profile_v31_count(void)
{
    uint8_t count = 0;
    for (uint32_t i = 0; i < BT_PROFILE_MAX; i++) {
        if (bt_profiles[i].valid) {
            count++;
        }
    }
    return count;
}

static void bt_profile_v31_state_update(void)
{
    bt_profile_v31_state_last.table_hash = bt_profile_v31_table_hash();
    bt_profile_v31_state_last.profile0_valid = bt_profiles[0].valid ? 1 : 0;
    bt_profile_v31_state_last.profile0_map_size =
        bt_profiles[0].valid ? bt_profiles[0].in_cfg.map_size : 0;
    bt_profile_v31_state_last.profile0_hash =
        bt_profiles[0].valid ? bt_profile_v31_cfg_hash(&bt_profiles[0].in_cfg) : 0;
    bt_profile_v31_state_last.runtime_hash =
        bt_profile_v31_cfg_hash(&config.in_cfg[0]);
    bt_profile_v31_state_last.seq = bt_profile_v31_state_last.seq;
    bt_profile_v31_state_last.profile_count = bt_profile_v31_count();
    bt_profile_v31_state_last.config_source = (uint8_t)config_get_src();
}

#define BT_PROFILE_V32_RING_CAPACITY 64

struct bt_profile_v32_ring_event {
    uint8_t kind;
    uint8_t event_type;
    int8_t profile_index;
    uint8_t config_source;
    uint32_t seq;
    int32_t result;
    uint32_t hash_before;
    uint32_t hash_after;
} __packed;

static struct bt_profile_v32_ring_event bt_profile_v32_ring[BT_PROFILE_V32_RING_CAPACITY];
static uint8_t bt_profile_v32_ring_head;
static uint8_t bt_profile_v32_ring_count;

static void bt_profile_v32_ring_push(uint8_t type, int8_t profile_index,
                                     uint8_t config_source, uint32_t seq,
                                     int32_t result, uint32_t before,
                                     uint32_t after)
{
    struct bt_profile_v32_ring_event *event =
        &bt_profile_v32_ring[bt_profile_v32_ring_head];

    event->kind = 5;
    event->event_type = type;
    event->profile_index = profile_index;
    event->config_source = config_source;
    event->seq = seq;
    event->result = result;
    event->hash_before = before;
    event->hash_after = after;

    bt_profile_v32_ring_head =
        (uint8_t)((bt_profile_v32_ring_head + 1U) %
                  BT_PROFILE_V32_RING_CAPACITY);

    if (bt_profile_v32_ring_count < BT_PROFILE_V32_RING_CAPACITY) {
        bt_profile_v32_ring_count++;
    }
}

static uint8_t bt_profile_v32_ring_physical_index(uint8_t logical_index)
{
    uint8_t oldest =
        (uint8_t)((bt_profile_v32_ring_head +
                   BT_PROFILE_V32_RING_CAPACITY -
                   bt_profile_v32_ring_count) %
                  BT_PROFILE_V32_RING_CAPACITY);

    return (uint8_t)((oldest + logical_index) %
                     BT_PROFILE_V32_RING_CAPACITY);
}

uint32_t config_bt_profile_v32_ring_meta(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};
    uint32_t next_seq = bt_profile_v31_state_last.seq + 1U;
    uint32_t oldest_seq = 0;
    uint32_t newest_seq = 0;

    payload[0] = 4;
    payload[1] = bt_profile_v32_ring_count;
    payload[2] = BT_PROFILE_V32_RING_CAPACITY;
    payload[3] = bt_profile_v32_ring_head;

    memcpy(&payload[4], &next_seq, 4);

    if (bt_profile_v32_ring_count > 0) {
        uint8_t first = bt_profile_v32_ring_physical_index(0);
        uint8_t last = bt_profile_v32_ring_physical_index(
            (uint8_t)(bt_profile_v32_ring_count - 1U));
        oldest_seq = bt_profile_v32_ring[first].seq;
        newest_seq = bt_profile_v32_ring[last].seq;
    }

    memcpy(&payload[8], &oldest_seq, 4);
    memcpy(&payload[12], &newest_seq, 4);

    uint32_t profile0_hash =
        bt_profiles[0].valid ?
        bt_profile_v31_cfg_hash(&bt_profiles[0].in_cfg) : 0U;
    memcpy(&payload[16], &profile0_hash, 4);

    if (!data || max_len < sizeof(payload)) {
        return 0;
    }

    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

uint32_t config_bt_profile_v32_ring_event(uint8_t logical_index,
                                          uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};

    if (logical_index >= bt_profile_v32_ring_count) {
        return 0;
    }

    uint8_t physical =
        bt_profile_v32_ring_physical_index(logical_index);

    memcpy(payload, &bt_profile_v32_ring[physical], sizeof(payload));

    if (!data || max_len < sizeof(payload)) {
        return 0;
    }

    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}


#define BT_PROFILE_V35_RING_CAPACITY 128

struct bt_profile_v35_event {
    uint8_t kind;
    uint8_t type;
    int8_t profile;
    uint8_t source;
    uint32_t seq;
    int32_t result;
    uint32_t profile_before;
    uint32_t profile_after;
    uint32_t table_before;
    uint32_t table_after;
    uint32_t runtime_before;
    uint32_t runtime_after;
} __packed;

struct bt_profile_v35_identity {
    uint8_t valid;
    uint8_t addr_type;
    uint16_t reserved;
    uint32_t system_id;
    uint32_t source_id;
    uint32_t source_out_idx;
    uint8_t bdaddr[6];
    uint16_t reserved2;
    uint32_t profile_index;
    uint32_t profile_hash;
} __packed;

struct bt_profile_v35_find {
    int32_t result;
    struct bt_profile_v35_identity lookup;
    struct bt_profile_v35_identity found;
} __packed;

struct bt_profile_v35_nvs {
    int32_t open_err;
    int32_t get_err;
    uint32_t stored_size;
    uint32_t expected_size;
    uint32_t table_hash_after_load;
    uint32_t profile0_hash_after_load;
} __packed;

static struct bt_profile_v35_event bt_profile_v35_ring[BT_PROFILE_V35_RING_CAPACITY];
static uint8_t bt_profile_v35_ring_head;
static uint8_t bt_profile_v35_ring_count;
static struct bt_profile_v35_find bt_profile_v35_find_last;
static struct bt_profile_v35_nvs bt_profile_v35_nvs_last;

static uint32_t bt_profile_v35_profile_hash(int32_t index)
{
    if (index < 0 || index >= BT_PROFILE_MAX ||
        !bt_profiles[index].valid) {
        return 0;
    }
    return bt_profile_v30_cfg_hash(&bt_profiles[index].in_cfg);
}

static void bt_profile_v35_identity_from_profile(
    struct bt_profile_v35_identity *out, int32_t index)
{
    memset(out, 0, sizeof(*out));
    if (index < 0 || index >= BT_PROFILE_MAX ||
        !bt_profiles[index].valid) {
        return;
    }
    out->valid = 1;
    out->addr_type = bt_profiles[index].addr_type;
    out->system_id = bt_profiles[index].system_id;
    out->source_id = bt_profiles[index].source_id;
    out->source_out_idx = bt_profiles[index].source_out_idx;
    memcpy(out->bdaddr, bt_profiles[index].bdaddr, sizeof(out->bdaddr));
    out->profile_index = (uint32_t)index;
    out->profile_hash = bt_profile_v35_profile_hash(index);
}

static void bt_profile_v35_ring_push(
    uint8_t type, int8_t profile, uint8_t source, uint32_t seq,
    int32_t result, uint32_t pb, uint32_t pa,
    uint32_t tb, uint32_t ta, uint32_t rb, uint32_t ra)
{
    struct bt_profile_v35_event *event =
        &bt_profile_v35_ring[bt_profile_v35_ring_head];

    event->kind = 9;
    event->type = type;
    event->profile = profile;
    event->source = source;
    event->seq = seq;
    event->result = result;
    event->profile_before = pb;
    event->profile_after = pa;
    event->table_before = tb;
    event->table_after = ta;
    event->runtime_before = rb;
    event->runtime_after = ra;

    bt_profile_v35_ring_head =
        (uint8_t)((bt_profile_v35_ring_head + 1U) %
                  BT_PROFILE_V35_RING_CAPACITY);
    if (bt_profile_v35_ring_count < BT_PROFILE_V35_RING_CAPACITY) {
        bt_profile_v35_ring_count++;
    }
}

uint32_t config_bt_profile_v35_ring_meta(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};
    uint32_t oldest = 0;
    uint32_t newest = 0;
    payload[0] = 9;
    payload[1] = bt_profile_v35_ring_count;
    payload[2] = BT_PROFILE_V35_RING_CAPACITY;
    payload[3] = bt_profile_v35_ring_head;
    if (bt_profile_v35_ring_count > 0) {
        uint8_t first = (uint8_t)((bt_profile_v35_ring_head +
            BT_PROFILE_V35_RING_CAPACITY - bt_profile_v35_ring_count) %
            BT_PROFILE_V35_RING_CAPACITY);
        uint8_t last = (uint8_t)((bt_profile_v35_ring_head +
            BT_PROFILE_V35_RING_CAPACITY - 1U) %
            BT_PROFILE_V35_RING_CAPACITY);
        oldest = bt_profile_v35_ring[first].seq;
        newest = bt_profile_v35_ring[last].seq;
    }
    memcpy(&payload[4], &oldest, 4);
    memcpy(&payload[8], &newest, 4);
    {
        uint32_t ph = bt_profile_v35_profile_hash(0);
        uint32_t rh = bt_profile_v30_cfg_hash(&config.in_cfg[0]);
        memcpy(&payload[12], &ph, 4);
        memcpy(&payload[16], &rh, 4);
    }
    if (!data || max_len < sizeof(payload)) {
        return 0;
    }
    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

uint32_t config_bt_profile_v35_ring_event(
    uint8_t logical_index, uint8_t *data, uint32_t max_len)
{
    uint8_t first;
    uint8_t physical;
    if (logical_index >= bt_profile_v35_ring_count ||
        !data || max_len < 20) {
        return 0;
    }
    first = (uint8_t)((bt_profile_v35_ring_head +
        BT_PROFILE_V35_RING_CAPACITY - bt_profile_v35_ring_count) %
        BT_PROFILE_V35_RING_CAPACITY);
    physical = (uint8_t)((first + logical_index) %
        BT_PROFILE_V35_RING_CAPACITY);
    memcpy(data, &bt_profile_v35_ring[physical], 20);
    return 20;
}

uint32_t config_bt_profile_v35_find_diag(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};
    payload[0] = 10;
    payload[1] = bt_profile_v35_find_last.lookup.valid;
    payload[2] = bt_profile_v35_find_last.found.valid;
    payload[3] = (uint8_t)bt_profile_v35_find_last.result;
    memcpy(&payload[4], &bt_profile_v35_find_last.lookup.system_id, 4);
    memcpy(&payload[8], &bt_profile_v35_find_last.found.system_id, 4);
    memcpy(&payload[12], bt_profile_v35_find_last.lookup.bdaddr, 6);
    payload[18] = bt_profile_v35_find_last.found.addr_type;
    payload[19] = bt_profile_v35_find_last.lookup.addr_type;
    if (!data || max_len < sizeof(payload)) {
        return 0;
    }
    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

uint32_t config_bt_profile_v35_find_extra_diag(
    uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};
    payload[0] = 11;
    memcpy(&payload[1], &bt_profile_v35_find_last.lookup.source_id, 4);
    memcpy(&payload[5], &bt_profile_v35_find_last.lookup.source_out_idx, 4);
    memcpy(&payload[9], &bt_profile_v35_find_last.found.source_id, 4);
    memcpy(&payload[13], &bt_profile_v35_find_last.found.source_out_idx, 4);
    memcpy(&payload[17], bt_profile_v35_find_last.found.bdaddr, 3);
    if (!data || max_len < sizeof(payload)) {
        return 0;
    }
    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

uint32_t config_bt_profile_v35_nvs_diag(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};
    payload[0] = 12;
    memcpy(&payload[1], &bt_profile_v35_nvs_last.open_err, 4);
    memcpy(&payload[5], &bt_profile_v35_nvs_last.get_err, 4);
    memcpy(&payload[9], &bt_profile_v35_nvs_last.stored_size, 4);
    memcpy(&payload[13], &bt_profile_v35_nvs_last.expected_size, 4);
    memcpy(&payload[17], &bt_profile_v35_nvs_last.profile0_hash_after_load, 3);
    if (!data || max_len < sizeof(payload)) {
        return 0;
    }
    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

static uint32_t bt_profile_v35_profile_hash(int32_t index);
static void bt_profile_v35_ring_push(
    uint8_t type, int8_t profile, uint8_t source, uint32_t seq,
    int32_t result, uint32_t pb, uint32_t pa,
    uint32_t tb, uint32_t ta, uint32_t rb, uint32_t ra);

static void bt_profile_v31_event(uint8_t type, int8_t profile_index,
                                 int32_t result, uint32_t before,
                                 uint32_t after)
{
    bt_profile_v31_state_last.seq++;
    bt_profile_v31_last.kind = 1;
    bt_profile_v31_last.event_type = type;
    bt_profile_v31_last.profile_index = profile_index;
    bt_profile_v31_last.config_source = (uint8_t)config_get_src();
    bt_profile_v31_last.seq = bt_profile_v31_state_last.seq;
    bt_profile_v31_last.result = result;
    bt_profile_v31_last.hash_before = before;
    bt_profile_v31_last.hash_after = after;

    bt_profile_v32_ring_push(
        type,
        profile_index,
        bt_profile_v31_last.config_source,
        bt_profile_v31_last.seq,
        result,
        before,
        after);

    bt_profile_v35_ring_push(
        type,
        profile_index,
        bt_profile_v31_last.config_source,
        bt_profile_v31_last.seq,
        result,
        before,
        after,
        bt_profile_v30_table_hash(),
        bt_profile_v30_table_hash(),
        bt_profile_v30_cfg_hash(&config.in_cfg[0]),
        bt_profile_v30_cfg_hash(&config.in_cfg[0]));

    bt_profile_v31_state_update();
}



uint32_t config_bt_profile_v31_event_diag(uint8_t *data, uint32_t max_len)
{
    const uint32_t size = sizeof(bt_profile_v31_last);
    if (!data || max_len < size) {
        return 0;
    }
    memcpy(data, &bt_profile_v31_last, size);
    return size;
}

uint32_t config_bt_profile_v31_state_diag(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};
    payload[0] = 2;
    payload[1] = bt_profile_v31_state_last.profile0_valid;
    payload[2] = bt_profile_v31_state_last.profile0_map_size;
    payload[3] = bt_profile_v31_state_last.profile_count;
    memcpy(&payload[4], &bt_profile_v31_state_last.profile0_hash, 4);
    memcpy(&payload[8], &bt_profile_v31_state_last.table_hash, 4);
    memcpy(&payload[12], &bt_profile_v31_state_last.runtime_hash, 4);
    memcpy(&payload[16], &bt_profile_v31_state_last.seq, 4);

    if (!data || max_len < sizeof(payload)) {
        return 0;
    }
    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

uint32_t config_bt_profile_v31_nvs_diag(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};
    payload[0] = 3;
    memcpy(&payload[1], &bt_profile_v31_state_last.nvs_open_err, 4);
    memcpy(&payload[5], &bt_profile_v31_state_last.nvs_get_err, 4);
    memcpy(&payload[9], &bt_profile_v31_state_last.stored_size, 4);
    memcpy(&payload[13], &bt_profile_v31_state_last.expected_size, 4);
    memcpy(&payload[17], &bt_profile_v31_state_last.loaded_hash, 4);

    if (!data || max_len < sizeof(payload)) {
        return 0;
    }
    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

struct bt_profile_v36_save_diag {
    int32_t open_err;
    int32_t set_err;
    int32_t commit_err;
    uint32_t profile_size;
    uint32_t used_entries;
    uint32_t free_entries;
    uint32_t total_entries;
    uint32_t table_hash;
} __packed;

static struct bt_profile_v36_save_diag bt_profile_v36_save_last;

uint32_t config_bt_profile_v36_save_diag(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};

    payload[0] = 13;
    memcpy(&payload[1], &bt_profile_v36_save_last.open_err, 4);
    memcpy(&payload[5], &bt_profile_v36_save_last.set_err, 4);
    memcpy(&payload[9], &bt_profile_v36_save_last.commit_err, 4);
    memcpy(&payload[13], &bt_profile_v36_save_last.free_entries, 4);
    memcpy(&payload[17], &bt_profile_v36_save_last.profile_size, 3);

    if (!data || max_len < sizeof(payload)) {
        return 0;
    }

    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

static int32_t bt_profile_save_one(uint8_t profile_id)
{
    if (profile_id >= BT_PROFILE_MAX) {
        return -1;
    }

    nvs_handle_t nvs = 0;
    esp_err_t oe = nvs_open(BT_PROFILE_NVS_NS, NVS_READWRITE, &nvs);
    esp_err_t se = ESP_FAIL;
    esp_err_t ce = ESP_FAIL;
    esp_err_t fe = oe;
    char key[16];

    snprintf(key, sizeof(key), BT_PROFILE_NVS_PROFILE_KEY_FMT,
             (unsigned)profile_id);

    if (oe == ESP_OK) {
        nvs_stats_t stats = {0};
        nvs_get_stats(BT_PROFILE_NVS_NS, &stats);

        bt_profile_v36_save_last.open_err = oe;
        bt_profile_v36_save_last.set_err = ESP_FAIL;
        bt_profile_v36_save_last.commit_err = ESP_FAIL;
        bt_profile_v36_save_last.profile_size =
            (uint32_t)sizeof(bt_profiles[profile_id]);
        bt_profile_v36_save_last.used_entries = stats.used_entries;
        bt_profile_v36_save_last.free_entries = stats.free_entries;
        bt_profile_v36_save_last.total_entries = stats.total_entries;
        bt_profile_v36_save_last.table_hash =
            bt_profile_v30_table_hash();

        se = nvs_set_blob(nvs, key, &bt_profiles[profile_id],
                          sizeof(bt_profiles[profile_id]));
        bt_profile_v36_save_last.set_err = se;

        if (se == ESP_OK) {
            ce = nvs_commit(nvs);
            fe = ce;
        } else {
            fe = se;
        }

        nvs_stats_t stats_after = {0};
        nvs_get_stats(BT_PROFILE_NVS_NS, &stats_after);
        bt_profile_v36_save_last.commit_err = ce;
        bt_profile_v36_save_last.used_entries = stats_after.used_entries;
        bt_profile_v36_save_last.free_entries = stats_after.free_entries;
        bt_profile_v36_save_last.total_entries = stats_after.total_entries;
        bt_profile_v36_save_last.table_hash =
            bt_profile_v30_table_hash();

        nvs_close(nvs);
    }

    uint32_t before = bt_profile_v30_table_hash();
    uint32_t after = bt_profile_v30_table_hash();
    uint32_t aux = ((uint32_t)se & 0xffffU) |
                   (((uint32_t)ce & 0xffffU) << 16);

    bt_profile_v30_trace(V30_SAVE, (int8_t)profile_id,
                         bt_profiles[profile_id].source_out_idx,
                         fe, before, after, aux);
    bt_profile_v31_event(2, (int8_t)profile_id, fe, before, after);

    printf("# V36_SAVE profile=%u open=%ld set=%ld commit=%ld size=%lu\n",
           (unsigned)profile_id, (long)oe, (long)se, (long)ce,
           (unsigned long)sizeof(bt_profiles[profile_id]));

    return fe == ESP_OK ? 0 : -1;
}

static int32_t bt_profile_save(void)
{
    nvs_handle_t nvs = 0;
    esp_err_t oe = nvs_open(BT_PROFILE_NVS_NS, NVS_READWRITE, &nvs);
    if (oe != ESP_OK) {
        return -1;
    }

    esp_err_t first_error = ESP_OK;
    for (uint8_t i = 0; i < BT_PROFILE_MAX; i++) {
        char key[16];
        snprintf(key, sizeof(key), BT_PROFILE_NVS_PROFILE_KEY_FMT,
                 (unsigned)i);

        esp_err_t err;
        if (bt_profiles[i].valid) {
            err = nvs_set_blob(nvs, key, &bt_profiles[i],
                               sizeof(bt_profiles[i]));
        } else {
            err = nvs_erase_key(nvs, key);
            if (err == ESP_ERR_NVS_NOT_FOUND) {
                err = ESP_OK;
            }
        }

        if (err != ESP_OK && first_error == ESP_OK) {
            first_error = err;
        }
    }

    esp_err_t ce = ESP_FAIL;
    if (first_error == ESP_OK) {
        ce = nvs_commit(nvs);
        first_error = ce;
    }

    nvs_close(nvs);
    return first_error == ESP_OK ? 0 : -1;
}


struct bt_profile_nvs_diag {
    int32_t open_err;
    int32_t get_err;
    uint32_t stored_size;
    uint32_t expected_size;
    uint8_t action_loaded;
    uint8_t profile0_valid;
    uint16_t reserved;
    uint32_t profile0_hash;
} __packed;

static struct bt_profile_nvs_diag bt_profile_nvs_last;

void config_bt_profile_init(void)
{
    memset(&bt_profile_nvs_last, 0, sizeof(bt_profile_nvs_last));
    esp_err_t open_err = ESP_FAIL;
    esp_err_t get_err = ESP_FAIL;
    size_t requested_size = sizeof(bt_profiles);
    size_t stored_size = 0;

    memset(bt_profiles, 0, sizeof(bt_profiles));

    nvs_handle_t nvs = 0;
    open_err = nvs_open(BT_PROFILE_NVS_NS, NVS_READONLY, &nvs);

    if (open_err == ESP_OK) {
        stored_size = requested_size;
        get_err = nvs_get_blob(nvs, BT_PROFILE_NVS_KEY, bt_profiles, &stored_size);
        if (get_err == ESP_OK && stored_size == sizeof(bt_profiles)) bt_profile_v30_snap.loaded_blob_hash = bt_profile_v30_table_hash();
        nvs_close(nvs);
    }

    bt_profile_nvs_last.open_err = open_err;
    bt_profile_nvs_last.get_err = get_err;
    bt_profile_nvs_last.stored_size = (uint32_t)stored_size;
    bt_profile_nvs_last.expected_size = (uint32_t)sizeof(bt_profiles);
    if (open_err != ESP_OK || get_err != ESP_OK ||
        stored_size != sizeof(bt_profiles)) {
        printf("# V28_NVS_LOAD open=%ld get=%ld stored=%lu expected=%lu action=EMPTY\n",
               (long)open_err,
               (long)get_err,
               (unsigned long)stored_size,
               (unsigned long)sizeof(bt_profiles));
        bt_profile_nvs_last.action_loaded = 0;
        memset(bt_profiles, 0, sizeof(bt_profiles));
    } else {
        printf("# V28_NVS_LOAD open=%ld get=%ld stored=%lu expected=%lu action=LOADED\n",
               (long)open_err,
               (long)get_err,
               (unsigned long)stored_size,
               (unsigned long)sizeof(bt_profiles));
        bt_profile_nvs_last.action_loaded = 1;
    }

    bt_profile_v30_snap.open_err=open_err; bt_profile_v30_snap.get_err=get_err;
    bt_profile_v30_snap.stored_size=(uint32_t)stored_size; bt_profile_v30_snap.expected_size=(uint32_t)sizeof(bt_profiles);
    bt_profile_v30_trace(V30_NVS_LOAD,-1,0,(open_err!=ESP_OK)?open_err:get_err,0,bt_profile_v30_table_hash(),(uint32_t)stored_size);

    bt_profile_v31_event(
        1, -1,
        (open_err != ESP_OK) ? open_err : get_err,
        0,
        bt_profile_v30_table_hash());

    bt_profile_v35_nvs_last.open_err = open_err;
    bt_profile_v35_nvs_last.get_err = get_err;
    bt_profile_v35_nvs_last.stored_size = (uint32_t)stored_size;
    bt_profile_v35_nvs_last.expected_size = (uint32_t)sizeof(bt_profiles);
    bt_profile_v35_nvs_last.table_hash_after_load =
        bt_profile_v30_table_hash();
    bt_profile_v35_nvs_last.profile0_hash_after_load =
        bt_profile_v35_profile_hash(0);


    if (open_err == ESP_OK) {
        nvs_handle_t profile_nvs = 0;
        if (nvs_open(BT_PROFILE_NVS_NS, NVS_READONLY, &profile_nvs) == ESP_OK) {
            for (uint8_t i = 0; i < BT_PROFILE_MAX; i++) {
                char key[16];
                size_t profile_size = sizeof(bt_profiles[i]);

                snprintf(key, sizeof(key), BT_PROFILE_NVS_PROFILE_KEY_FMT,
                         (unsigned)i);

                esp_err_t profile_get = nvs_get_blob(
                    profile_nvs, key, &bt_profiles[i], &profile_size);

                if (profile_get == ESP_OK &&
                    profile_size == sizeof(bt_profiles[i])) {
                    bt_profiles[i].valid = 1;
                }
            }
            nvs_close(profile_nvs);
        }
    }

    bt_profiles_loaded = true;

    for (uint32_t i = 0; i < BT_PROFILE_MAX; i++) {
        if (!bt_profiles[i].valid) {
            continue;
        }

        uint32_t hash = 2166136261u;
        uint32_t map_size = bt_profiles[i].in_cfg.map_size;
        if (map_size > ADAPTER_MAPPING_MAX) {
            map_size = ADAPTER_MAPPING_MAX;
        }

        const uint8_t *bytes = (const uint8_t *)bt_profiles[i].in_cfg.map_cfg;
        for (uint32_t j = 0; j < map_size * sizeof(struct map_cfg); j++) {
            hash ^= bytes[j];
            hash *= 16777619u;
        }

        printf("# V28_NVS_PROFILE index=%lu valid=%u addr_type=%u system=%u source_out=%u "
               "bdaddr=%02X:%02X:%02X:%02X:%02X:%02X map=%u hash=%08lx\n",
               (unsigned long)i,
               (unsigned)bt_profiles[i].valid,
               (unsigned)bt_profiles[i].addr_type,
               (unsigned)bt_profiles[i].system_id,
               (unsigned)bt_profiles[i].source_out_idx,
               bt_profiles[i].bdaddr[0],
               bt_profiles[i].bdaddr[1],
               bt_profiles[i].bdaddr[2],
               bt_profiles[i].bdaddr[3],
               bt_profiles[i].bdaddr[4],
               bt_profiles[i].bdaddr[5],
               (unsigned)bt_profiles[i].in_cfg.map_size,
               (unsigned long)hash);
    }
    bt_profile_nvs_last.profile0_valid = bt_profiles[0].valid ? 1 : 0;

    if (bt_profiles[0].valid) {
        uint32_t hash = 2166136261u;
        uint32_t map_size = bt_profiles[0].in_cfg.map_size;
        if (map_size > ADAPTER_MAPPING_MAX) {
            map_size = ADAPTER_MAPPING_MAX;
        }
        const uint8_t *bytes = (const uint8_t *)bt_profiles[0].in_cfg.map_cfg;
        for (uint32_t j = 0; j < map_size * sizeof(struct map_cfg); j++) {
            hash ^= bytes[j];
            hash *= 16777619u;
        }
        bt_profile_nvs_last.profile0_hash = hash;
    }

}



struct bt_profile_v34_ensure_diag {
    uint8_t kind;
    uint8_t is_new;
    int8_t profile_index;
    uint8_t found;
    uint32_t find_result;
    uint32_t table_before;
    uint32_t profile_before;
    uint32_t runtime_before;
    uint32_t table_after;
    uint32_t profile_after;
    uint32_t runtime_after;
    uint32_t seq;
} __packed;

static struct bt_profile_v34_ensure_diag bt_profile_v34_ensure_last;

uint32_t config_bt_profile_v34_ensure_diag(uint8_t *data, uint32_t max_len)
{
    const uint32_t size = sizeof(bt_profile_v34_ensure_last);
    if (!data || max_len < size) {
        return 0;
    }
    memcpy(data, &bt_profile_v34_ensure_last, size);
    return size;
}

uint32_t config_bt_profile_v34_ensure_pre_diag(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};

    payload[0] = 6;
    payload[1] = bt_profile_v34_ensure_last.is_new;
    payload[2] = (uint8_t)bt_profile_v34_ensure_last.profile_index;
    payload[3] = bt_profile_v34_ensure_last.found;
    memcpy(&payload[4], &bt_profile_v34_ensure_last.find_result, 4);
    memcpy(&payload[8], &bt_profile_v34_ensure_last.table_before, 4);
    memcpy(&payload[12], &bt_profile_v34_ensure_last.profile_before, 4);
    memcpy(&payload[16], &bt_profile_v34_ensure_last.runtime_before, 4);

    if (!data || max_len < sizeof(payload)) {
        return 0;
    }
    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

uint32_t config_bt_profile_v34_ensure_post_diag(uint8_t *data, uint32_t max_len)
{
    uint8_t payload[20] = {0};

    payload[0] = 7;
    memcpy(&payload[1], &bt_profile_v34_ensure_last.table_after, 4);
    memcpy(&payload[5], &bt_profile_v34_ensure_last.profile_after, 4);
    memcpy(&payload[9], &bt_profile_v34_ensure_last.runtime_after, 4);
    memcpy(&payload[13], &bt_profile_v34_ensure_last.seq, 4);
    memcpy(&payload[17], &bt_profile_v34_ensure_last.find_result, 4);

    if (!data || max_len < sizeof(payload)) {
        return 0;
    }
    memcpy(data, payload, sizeof(payload));
    return sizeof(payload);
}

int32_t config_bt_profile_ensure(uint8_t dev_id, uint8_t out_idx)
{
    struct bt_dev *device = NULL;

    /* Never create a controller profile from a console/game override. */
    if (wired_adapter.system_id == WIRED_AUTO ||
        out_idx >= WIRED_MAX_DEV ||
        config_get_src() != DEFAULT_CFG) {
        return -1;
    }

    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }

    if (bt_host_get_dev_from_id(dev_id, &device) < 0 || !device) {
        printf("# %s: unable to resolve device id %u\\n", __FUNCTION__, dev_id);
        return -1;
    }

    /*
     * This function is called from the controller connection path. At that
     * point the bt_dev object already contains the remote address, but
     * BT_DEV_HID_INIT_DONE is not guaranteed to be set yet for every
     * controller type. Requiring that flag caused first-connect profile
     * creation to be skipped.
     */
    uint8_t addr_type;
    uint8_t bdaddr[6];

    if (bt_profile_get_addr(device, &addr_type, bdaddr) < 0) {
        printf("# %s: unable to get remote address for device %u\\n",
               __FUNCTION__, dev_id);
        return -1;
    }
int32_t index = bt_profile_find(addr_type,
                                    (uint8_t)wired_adapter.system_id,
                                    bdaddr);
    bool is_new = false;

    uint32_t table_before_v35 = bt_profile_v30_table_hash();
    uint32_t runtime_before_v35 =
        bt_profile_v30_cfg_hash(&config.in_cfg[out_idx]);
    uint32_t profile_before_v35 = 0;

    if (index >= 0 &&
        index < BT_PROFILE_MAX &&
        bt_profiles[index].valid) {
        profile_before_v35 =
            bt_profile_v30_cfg_hash(&bt_profiles[index].in_cfg);
    }

    memset(&bt_profile_v35_find_last, 0,
           sizeof(bt_profile_v35_find_last));
    bt_profile_v35_find_last.result = index;
    bt_profile_v35_find_last.lookup.valid = 1;
    bt_profile_v35_find_last.lookup.addr_type = addr_type;
    bt_profile_v35_find_last.lookup.system_id =
        (uint8_t)wired_adapter.system_id;
    bt_profile_v35_find_last.lookup.source_id =
        bt_profile_source_id(device);
    bt_profile_v35_find_last.lookup.source_out_idx = out_idx;
    memcpy(bt_profile_v35_find_last.lookup.bdaddr,
           bdaddr, sizeof(bt_profile_v35_find_last.lookup.bdaddr));
    bt_profile_v35_identity_from_profile(
        &bt_profile_v35_find_last.found, index);

    bool metadata_changed = false;

    if (index < 0) {
        index = bt_profile_alloc();
        if (index < 0) {
            printf("# %s: profile table full\n", __FUNCTION__);
            return -1;
        }

        memset(&bt_profiles[index], 0, sizeof(bt_profiles[index]));
        bt_profiles[index].valid = 1;
        bt_profiles[index].addr_type = addr_type;
        bt_profiles[index].system_id = (uint8_t)wired_adapter.system_id;
        bt_profiles[index].source_id = bt_profile_source_id(device);
        memcpy(bt_profiles[index].bdaddr, bdaddr, 6);

        /*
         * Only a newly-created profile receives the current runtime
         * configuration. Once a MAC-keyed profile exists, its mapping is
         * authoritative and must never be overwritten by another
         * controller occupying the same runtime slot.
         */
        bt_profiles[index].source_out_idx = out_idx;
        memcpy(&bt_profiles[index].in_cfg,
               &config.in_cfg[out_idx],
               sizeof(struct in_cfg));
        is_new = true;
        metadata_changed = true;
    }

    uint8_t detected_source_id = bt_profile_source_id(device);
    if (detected_source_id != 0 && bt_profiles[index].source_id != detected_source_id) {
        bt_profiles[index].source_id = detected_source_id;
        metadata_changed = true;
    }

    /*
     * Preserve the actual Bluetooth name whenever the HID layer has seen it.
     * Known names are available through device->name; unknown devices keep
     * the raw name captured in bt_data.base.raw_name.
     */
    struct bt_data *runtime_bt_data =
        &bt_adapter.data[device->ids.id];

    const char *profile_name = NULL;
    if (device->name && device->name->name[0]) {
        profile_name = device->name->name;
    }
    else if (runtime_bt_data->base.raw_name[0]) {
        profile_name = runtime_bt_data->base.raw_name;
    }

    if (profile_name) {
        char new_name[sizeof(bt_profiles[index].name)] = {0};
        strncpy(new_name, profile_name, sizeof(new_name) - 1);
        if (strncmp(bt_profiles[index].name, new_name, sizeof(new_name)) != 0) {
            memcpy(bt_profiles[index].name, new_name, sizeof(new_name));
            metadata_changed = true;
        }

        uint8_t name_source_id = bt_profile_source_id_from_name(profile_name);
        if (name_source_id != 0 && bt_profiles[index].source_id != name_source_id) {
            bt_profiles[index].source_id = name_source_id;
            metadata_changed = true;
        }
    }

    if (metadata_changed && bt_profile_save_one((uint8_t)index) < 0) {
        printf("# %s: failed to persist profile %ld\\n", __FUNCTION__, (long)index);
        return -1;
    }


    uint32_t table_after_v35 = bt_profile_v30_table_hash();
    uint32_t runtime_after_v35 =
        bt_profile_v30_cfg_hash(&config.in_cfg[out_idx]);
    uint32_t profile_after_v35 =
        bt_profile_v35_profile_hash(index);

    bt_profile_v35_find_last.result = index;
    bt_profile_v35_identity_from_profile(
        &bt_profile_v35_find_last.found, index);

    bt_profile_v35_ring_push(
        3,
        (int8_t)index,
        (uint8_t)config_get_src(),
        0,
        is_new ? -1 : 0,
        profile_before_v35,
        profile_after_v35,
        table_before_v35,
        table_after_v35,
        runtime_before_v35,
        runtime_after_v35);
    printf("# %s: profile=%ld new=%u dev=%u out=%u addr=%02X:%02X:%02X:%02X:%02X:%02X name=%s\\n",
           __FUNCTION__, (long)index, is_new ? 1 : 0, dev_id, out_idx,
           bdaddr[5], bdaddr[4], bdaddr[3], bdaddr[2], bdaddr[1], bdaddr[0],
           bt_profiles[index].name);
    return index;
}

struct bt_profile_apply_diag {
    uint8_t valid;
    uint8_t dev_id;
    uint8_t out_idx;
    int8_t profile_index;
    uint8_t profile_source_out_idx;
    uint8_t map_size;
    uint8_t addr_type;
    uint8_t bdaddr[6];
    uint32_t map_hash_before;
    uint32_t map_hash_after;
} __packed;

static struct bt_profile_apply_diag bt_profile_apply_last;

static uint32_t bt_profile_apply_hash_cfg(const struct in_cfg *cfg)
{
    uint32_t hash = 2166136261u;
    uint32_t map_size = cfg->map_size;

    if (map_size > ADAPTER_MAPPING_MAX) {
        map_size = ADAPTER_MAPPING_MAX;
    }

    const uint8_t *bytes = (const uint8_t *)cfg->map_cfg;
    for (uint32_t i = 0; i < map_size * sizeof(struct map_cfg); i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }

    return hash;
}

struct bt_profile_addr_diag {
    uint8_t valid;
    uint8_t dev_id;
    uint8_t out_idx;
    int8_t profile_index;
    uint8_t addr_type;
    uint8_t profile_addr_type;
    uint8_t system_id;
    uint8_t profile_system_id;
    uint8_t source_id;
    uint8_t profile_source_id;
    uint8_t bdaddr[6];
} __packed;

static struct bt_profile_addr_diag bt_profile_addr_last;

uint32_t config_bt_profile_addr_diag(uint8_t *data, uint32_t max_len)
{
    const uint32_t record_size = sizeof(struct bt_profile_addr_diag);

    if (!data || max_len < record_size) {
        return 0;
    }

    memcpy(data, &bt_profile_addr_last, record_size);
    return record_size;
}


static uint32_t bt_profile_map_hash(const struct in_cfg *cfg)
{
    uint32_t hash = 2166136261u;
    uint32_t map_size = cfg->map_size;

    if (map_size > ADAPTER_MAPPING_MAX) {
        map_size = ADAPTER_MAPPING_MAX;
    }

    const uint8_t *bytes = (const uint8_t *)cfg->map_cfg;

    for (uint32_t i = 0; i < map_size * sizeof(struct map_cfg); i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }

    return hash;
}

struct bt_profile_hash_diag {
    uint8_t valid;
    int8_t profile_index;
    uint8_t addr_type;
    uint8_t system_id;
    uint8_t profile_map_size;
    uint8_t runtime_map_size;
    uint8_t bdaddr[6];
    uint32_t profile_hash;
    uint32_t runtime_hash;
} __packed;

static struct bt_profile_hash_diag bt_profile_hash_last;

uint32_t config_bt_profile_hash_diag(uint8_t *data, uint32_t max_len)
{
    const uint32_t size = sizeof(struct bt_profile_hash_diag);

    if (!data || max_len < size) {
        return 0;
    }

    memcpy(data, &bt_profile_hash_last, size);
    return size;
}




uint32_t config_bt_profile_nvs_diag(uint8_t *data, uint32_t max_len)
{
    const uint32_t size = sizeof(struct bt_profile_nvs_diag);
    if (!data || max_len < size) {
        return 0;
    }
    memcpy(data, &bt_profile_nvs_last, size);
    return size;
}

int32_t config_bt_profile_apply(uint8_t dev_id, uint8_t out_idx)
{
    struct bt_dev *device = NULL;

    memset(&bt_profile_apply_last, 0, sizeof(bt_profile_apply_last));
    bt_profile_apply_last.valid = 1;
    bt_profile_apply_last.dev_id = dev_id;
    bt_profile_apply_last.out_idx = out_idx;
    bt_profile_apply_last.profile_index = -1;

    /* Controller profile is the base/default layer. A console/game-specific
     * configuration is authoritative and must not be overwritten. */
    if (config_get_src() != DEFAULT_CFG) {
        return -2;
    }

    if (wired_adapter.system_id == WIRED_AUTO ||
        out_idx >= WIRED_MAX_DEV) {
        return -1;
    }

    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }

    if (bt_host_get_dev_from_id(dev_id, &device) < 0 || !device) {
        return -1;
    }

    uint8_t addr_type;
    uint8_t bdaddr[6];

    if (bt_profile_get_addr(device, &addr_type, bdaddr) < 0) {
        return -1;
    }

    bt_profile_apply_last.addr_type = addr_type;
    memcpy(bt_profile_apply_last.bdaddr, bdaddr, 6);
    bt_profile_apply_last.map_hash_before =
        bt_profile_apply_hash_cfg(&config.in_cfg[out_idx]);

    int32_t index = bt_profile_find(addr_type,
                                     (uint8_t)wired_adapter.system_id,
                                     bdaddr);

    memset(&bt_profile_hash_last, 0, sizeof(bt_profile_hash_last));
    bt_profile_hash_last.valid = 1;
    bt_profile_hash_last.profile_index = (int8_t)index;
    bt_profile_hash_last.addr_type = addr_type;
    bt_profile_hash_last.system_id = (uint8_t)wired_adapter.system_id;
    memcpy(bt_profile_hash_last.bdaddr, bdaddr, 6);

    if (index >= 0 && index < BT_PROFILE_MAX) {
        bt_profile_hash_last.profile_map_size = bt_profiles[index].in_cfg.map_size;
        bt_profile_hash_last.profile_hash =
            bt_profile_map_hash(&bt_profiles[index].in_cfg);
    }

    bt_profile_hash_last.runtime_map_size = config.in_cfg[out_idx].map_size;
    bt_profile_hash_last.runtime_hash =
        bt_profile_map_hash(&config.in_cfg[out_idx]);

    printf("# V26_PROFILE_HASH_APPLY profile=%ld profile_hash=%08lx runtime_hash=%08lx map=%u runtime_map=%u\n",
           (long)index,
           (unsigned long)bt_profile_hash_last.profile_hash,
           (unsigned long)bt_profile_hash_last.runtime_hash,
           (unsigned)bt_profile_hash_last.profile_map_size,
           (unsigned)bt_profile_hash_last.runtime_map_size);

    memset(&bt_profile_addr_last, 0, sizeof(bt_profile_addr_last));
    bt_profile_addr_last.valid = 1;
    bt_profile_addr_last.dev_id = dev_id;
    bt_profile_addr_last.out_idx = out_idx;
    bt_profile_addr_last.profile_index = (int8_t)index;
    bt_profile_addr_last.addr_type = addr_type;
    bt_profile_addr_last.system_id = (uint8_t)wired_adapter.system_id;
    memcpy(bt_profile_addr_last.bdaddr, bdaddr, 6);

    if (index >= 0 && index < BT_PROFILE_MAX) {
        bt_profile_addr_last.profile_addr_type = bt_profiles[index].addr_type;
        bt_profile_addr_last.profile_system_id = bt_profiles[index].system_id;
        bt_profile_addr_last.profile_source_id = bt_profiles[index].source_id;
    }

    printf("# V25_PROFILE_FIND dev=%ld out=%ld result=%ld addr_type=%u profile_addr_type=%u system=%u profile_system=%u source=%u profile_source=%u bdaddr=%02X:%02X:%02X:%02X:%02X:%02X\n",
           (long)dev_id, (long)out_idx, (long)index,
           (unsigned)bt_profile_addr_last.addr_type,
           (unsigned)bt_profile_addr_last.profile_addr_type,
           (unsigned)bt_profile_addr_last.system_id,
           (unsigned)bt_profile_addr_last.profile_system_id,
           (unsigned)bt_profile_addr_last.source_id,
           (unsigned)bt_profile_addr_last.profile_source_id,
           bdaddr[0], bdaddr[1], bdaddr[2], bdaddr[3], bdaddr[4], bdaddr[5]);
    bt_profile_apply_last.profile_index = (int8_t)index;

    if (index < 0) {
        bt_profile_apply_last.map_hash_after =
            bt_profile_apply_hash_cfg(&config.in_cfg[out_idx]);
        return -1;
    }

    bt_profile_apply_last.profile_source_out_idx =
        bt_profiles[index].source_out_idx;

    memcpy(&config.in_cfg[out_idx],
           &bt_profiles[index].in_cfg,
           sizeof(struct in_cfg));

    uint32_t map_size = config.in_cfg[out_idx].map_size;
    if (map_size > ADAPTER_MAPPING_MAX) {
        map_size = ADAPTER_MAPPING_MAX;
        config.in_cfg[out_idx].map_size = ADAPTER_MAPPING_MAX;
    }

    for (uint32_t i = 0; i < map_size; i++) {
        if (config.in_cfg[out_idx].map_cfg[i].dst_id ==
            bt_profiles[index].source_out_idx) {
            config.in_cfg[out_idx].map_cfg[i].dst_id = out_idx;
        }
    }

    bt_profile_apply_last.map_size = config.in_cfg[out_idx].map_size;
    bt_profile_apply_last.map_hash_after =
        bt_profile_apply_hash_cfg(&config.in_cfg[out_idx]);
    bt_profile_v31_event(4, (int8_t)index, index, bt_profile_apply_last.map_hash_before, bt_profile_apply_last.map_hash_after);
    bt_profile_v30_trace(V30_APPLY,(int8_t)index,out_idx,index,bt_profile_apply_last.map_hash_before,bt_profile_apply_last.map_hash_after,bt_profiles[index].source_out_idx);

    return index;
}

uint32_t config_bt_profile_apply_diag(uint8_t *data, uint32_t max_len)
{
    const uint32_t record_size = 20;

    if (!data || max_len < record_size) {
        return 0;
    }

    memcpy(data, &bt_profile_apply_last, record_size);
    return record_size;
}

int32_t config_bt_profile_update_identity(uint8_t dev_id)
{
    struct bt_dev *device = NULL;

    if (wired_adapter.system_id == WIRED_AUTO) {
        return -1;
    }

    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }

    if (bt_host_get_dev_from_id(dev_id, &device) < 0 || !device) {
        return -1;
    }

    uint8_t addr_type;
    uint8_t bdaddr[6];

    if (bt_profile_get_addr(device, &addr_type, bdaddr) < 0) {
        return -1;
    }

    int32_t index = bt_profile_find(addr_type,
                                    (uint8_t)wired_adapter.system_id,
                                    bdaddr);
    if (index < 0) {
        return 0;
    }

    struct bt_data *runtime_bt_data = NULL;
    if (device->ids.id >= 0 && device->ids.id < BT_MAX_DEV) {
        runtime_bt_data = &bt_adapter.data[device->ids.id];
    }

    const char *profile_name = NULL;
    if (device->name && device->name->name[0]) {
        profile_name = device->name->name;
    }
    else if (runtime_bt_data && runtime_bt_data->base.raw_name[0]) {
        profile_name = runtime_bt_data->base.raw_name;
    }

    uint8_t detected_source_id = bt_profile_source_id(device);
    if (detected_source_id == 0 && profile_name) {
        detected_source_id = bt_profile_source_id_from_name(profile_name);
    }

    bool changed = false;

    if (profile_name) {
        char name[sizeof(bt_profiles[index].name)] = {0};
        strncpy(name, profile_name, sizeof(name) - 1);
        if (strncmp(bt_profiles[index].name, name, sizeof(name)) != 0) {
            memcpy(bt_profiles[index].name, name, sizeof(name));
            changed = true;
        }
    }

    if (detected_source_id != 0 &&
        bt_profiles[index].source_id != detected_source_id) {
        bt_profiles[index].source_id = detected_source_id;
        changed = true;
    }

    if (changed) {
        if (bt_profile_save_one((uint8_t)index) < 0) {
            printf("# %s: failed to persist identity profile=%ld dev=%u\n",
                   __FUNCTION__, (long)index, (unsigned int)dev_id);
            return -1;
        }

        printf("# %s: profile=%ld dev=%u source=%u name=%s\n",
               __FUNCTION__, (long)index, (unsigned int)dev_id,
               (unsigned int)bt_profiles[index].source_id,
               bt_profiles[index].name);
    }

    return index;
}

static uint32_t bt_profile_diag_hash_cfg(const struct in_cfg *cfg)
{
    /* FNV-1a over exactly the persisted map_cfg bytes. */
    uint32_t hash = 2166136261u;
    uint32_t map_size = cfg->map_size;

    if (map_size > ADAPTER_MAPPING_MAX) {
        map_size = ADAPTER_MAPPING_MAX;
    }

    const uint8_t *bytes = (const uint8_t *)cfg->map_cfg;
    for (uint32_t i = 0; i < map_size * sizeof(struct map_cfg); i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }

    return hash;
}

uint32_t config_bt_profile_persistence_diag(uint8_t profile_id,
                                             uint8_t *data,
                                             uint32_t max_len)
{
    /*
     * Diagnostic record: exactly 20 bytes.
     *  0      profile map_size
     *  1-4    profile (persisted/loaded) map hash
     *  5      runtime map_size
     *  6-9    runtime map hash
     * 10      profile source_out_idx
     * 11      runtime out_idx used for the comparison
     * 12-19  first map_cfg (8 bytes), copied from the profile
     */
    const uint32_t record_size = 20;

    if (!data || max_len < record_size || profile_id >= BT_PROFILE_MAX) {
        return 0;
    }

    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }

    if (!bt_profiles[profile_id].valid) {
        return 0;
    }

    uint8_t out_idx = bt_profiles[profile_id].source_out_idx;
    if (out_idx >= WIRED_MAX_DEV) {
        return 0;
    }

    memset(data, 0, record_size);

    const struct in_cfg *profile_cfg = &bt_profiles[profile_id].in_cfg;
    const struct in_cfg *runtime_cfg = &config.in_cfg[out_idx];

    uint32_t profile_hash = bt_profile_diag_hash_cfg(profile_cfg);
    uint32_t runtime_hash = bt_profile_diag_hash_cfg(runtime_cfg);

    data[0] = profile_cfg->map_size;
    data[1] = (uint8_t)(profile_hash & 0xff);
    data[2] = (uint8_t)((profile_hash >> 8) & 0xff);
    data[3] = (uint8_t)((profile_hash >> 16) & 0xff);
    data[4] = (uint8_t)((profile_hash >> 24) & 0xff);

    data[5] = runtime_cfg->map_size;
    data[6] = (uint8_t)(runtime_hash & 0xff);
    data[7] = (uint8_t)((runtime_hash >> 8) & 0xff);
    data[8] = (uint8_t)((runtime_hash >> 16) & 0xff);
    data[9] = (uint8_t)((runtime_hash >> 24) & 0xff);

    data[10] = bt_profiles[profile_id].source_out_idx;
    data[11] = out_idx;

    if (profile_cfg->map_size > 0) {
        memcpy(&data[12], &profile_cfg->map_cfg[0], sizeof(struct map_cfg));
    }

    return record_size;
}

uint32_t config_bt_profile_identity_diag(uint8_t profile_id,
                                           uint8_t *data,
                                           uint32_t max_len)
{
    /*
     * Diagnostic record (19 bytes, fits in a single ATT response):
     *  0-5   stored profile address
     *  6-11  current device address
     * 12-17  BLE identity address
     * 18    flags:
     *        bit 0 = current device matched
     *        bit 1 = identity address valid
     *
     * Address types are omitted from this compact diagnostic. The goal is to
     * compare the actual six-byte addresses across a power cycle.
     */
    const uint32_t record_size = 19;

    if (!data || max_len < record_size || profile_id >= BT_PROFILE_MAX) {
        return 0;
    }

    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }

    if (!bt_profiles[profile_id].valid) {
        return 0;
    }

    memset(data, 0, record_size);
    memcpy(&data[0], bt_profiles[profile_id].bdaddr, 6);

    for (uint32_t dev_id = 0; dev_id < BT_MAX_DEV; dev_id++) {
        struct bt_dev *device = NULL;

        if (bt_host_get_dev_from_id(dev_id, &device) < 0 || !device) {
            continue;
        }

        bool name_match = false;

        if (bt_profiles[profile_id].name[0]) {
            if (device->name && device->name->name[0] &&
                strncmp(device->name->name,
                        bt_profiles[profile_id].name,
                        sizeof(bt_profiles[profile_id].name)) == 0) {
                name_match = true;
            }

            if (!name_match &&
                device->ids.id >= 0 && device->ids.id < BT_MAX_DEV) {
                struct bt_data *bt_data = &bt_adapter.data[device->ids.id];

                if (bt_data->base.raw_name[0] &&
                    strncmp(bt_data->base.raw_name,
                            bt_profiles[profile_id].name,
                            sizeof(bt_profiles[profile_id].name)) == 0) {
                    name_match = true;
                }
            }
        }

        if (!name_match) {
            continue;
        }

        data[18] |= BIT(0);

        if (atomic_test_bit(&device->flags, BT_DEV_IS_BLE)) {
            memcpy(&data[6], device->le_remote_bdaddr.a.val, 6);

            if (device->le_identity_valid) {
                memcpy(&data[12], device->le_identity_addr.a.val, 6);
                data[18] |= BIT(1);
            }
        }
        else {
            memcpy(&data[6], device->remote_bdaddr, 6);
        }

        break;
    }

    return record_size;
}


void config_bt_profile_sync(void)
{
    if (wired_adapter.system_id == WIRED_AUTO) {
        return;
    }

    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }

    bool changed = false;

    for (uint32_t dev_id = 0; dev_id < BT_MAX_DEV; dev_id++) {
        struct bt_dev *device = NULL;

        if (bt_host_get_dev_from_id(dev_id, &device) < 0 || !device) {
            continue;
        }

        if (!atomic_test_bit(&device->flags, BT_DEV_HID_INIT_DONE)) {
            continue;
        }

        uint8_t addr_type;
        uint8_t bdaddr[6];

        if (bt_profile_get_addr(device, &addr_type, bdaddr) < 0) {
            continue;
        }

        int32_t index = bt_profile_find(addr_type,
                                        (uint8_t)wired_adapter.system_id,
                                        bdaddr);

        /*
         * Existing MAC-keyed mappings remain authoritative. This sync path
         * may create a missing profile, but never replaces its in_cfg mapping.
         */
        if (index < 0) {
            uint8_t out_idx = device->ids.out_idx;
            if (out_idx >= WIRED_MAX_DEV) {
                continue;
            }

            index = bt_profile_alloc();
            if (index < 0) {
                printf("# %s: profile table full\n", __FUNCTION__);
                continue;
            }

            memset(&bt_profiles[index], 0, sizeof(bt_profiles[index]));
            bt_profiles[index].valid = 1;
            bt_profiles[index].addr_type = addr_type;
            bt_profiles[index].system_id = (uint8_t)wired_adapter.system_id;
            bt_profiles[index].source_id = bt_profile_source_id(device);
            bt_profiles[index].source_out_idx = out_idx;
            memcpy(bt_profiles[index].bdaddr, bdaddr, 6);
            memcpy(&bt_profiles[index].in_cfg,
                   &config.in_cfg[out_idx],
                   sizeof(struct in_cfg));
            changed = true;

            printf("# %s: discovered new profile=%ld dev=%lu out=%u\n",
                   __FUNCTION__, (long)index, (unsigned long)dev_id, out_idx);
        }

        struct bt_data *runtime_bt_data = NULL;
        if (device->ids.id >= 0 && device->ids.id < BT_MAX_DEV) {
            runtime_bt_data = &bt_adapter.data[device->ids.id];
        }

        const char *profile_name = NULL;

        if (device->name && device->name->name[0]) {
            profile_name = device->name->name;
        }
        else if (runtime_bt_data && runtime_bt_data->base.raw_name[0]) {
            profile_name = runtime_bt_data->base.raw_name;
        }
        else if (bt_profiles[index].name[0]) {
            /*
             * Last-resort identity source: use the profile's already stored
             * Bluetooth name. This is what repairs a profile whose Source ID
             * was originally created as zero.
             */
            profile_name = bt_profiles[index].name;
        }

        /*
         * Structured controller type/quirk is preferred. If it cannot
         * identify the Source, derive it deterministically from the name.
         */
        uint8_t detected_source_id = bt_profile_source_id(device);

        if (profile_name) {
            uint8_t name_source_id =
                bt_profile_source_id_from_name(profile_name);

            if (detected_source_id == 0 && name_source_id != 0) {
                detected_source_id = name_source_id;
            }

            char name[sizeof(bt_profiles[index].name)] = {0};
            strncpy(name, profile_name, sizeof(name) - 1);

            if (strncmp(bt_profiles[index].name, name, sizeof(name)) != 0) {
                memcpy(bt_profiles[index].name, name, sizeof(name));
                changed = true;
            }
        }

        /*
         * If the existing profile has Source ID 0, a recognized stored name
         * is allowed to repair it. For a nonzero Source ID we retain the
         * structured controller identification unless the name supplies a
         * better known Source.
         */
        if (detected_source_id != 0 &&
            bt_profiles[index].source_id != detected_source_id) {
            bt_profiles[index].source_id = detected_source_id;
            changed = true;
        }
    }

    if (changed) {
        bt_profile_save();
    }
}


void config_bt_profile_clear(void)
{
    uint32_t before = bt_profile_v30_table_hash();

    memset(bt_profiles, 0, sizeof(bt_profiles));
    bt_profiles_loaded = true;

    /* Clear both the current per-profile records and the legacy profile blob. */
    int32_t result = bt_profile_save();

    nvs_handle_t nvs = 0;
    esp_err_t legacy_open = nvs_open(BT_PROFILE_NVS_NS, NVS_READWRITE, &nvs);

    if (legacy_open == ESP_OK) {
        esp_err_t legacy_erase =
            nvs_erase_key(nvs, BT_PROFILE_NVS_KEY);

        if (legacy_erase == ESP_OK ||
            legacy_erase == ESP_ERR_NVS_NOT_FOUND) {
            esp_err_t legacy_commit = nvs_commit(nvs);
            if (legacy_commit != ESP_OK) {
                result = -1;
            }
        }
        else {
            result = -1;
        }

        nvs_close(nvs);
    }
    else {
        result = -1;
    }

    bt_profile_v31_event(
        7,
        -1,
        result,
        before,
        bt_profile_v30_table_hash());

    bt_profile_v30_trace(
        V30_CLEAR,
        -1,
        0,
        result,
        before,
        bt_profile_v30_table_hash(),
        0);
}


int32_t config_bt_profile_read(uint8_t profile_id, uint8_t *data,
                               uint32_t offset, uint32_t max_len)
{
    if (profile_id >= BT_PROFILE_MAX || !data) {
        return -1;
    }
    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }
    if (!bt_profiles[profile_id].valid) {
        return -1;
    }
    if (offset >= sizeof(struct in_cfg)) {
        return 0;
    }

    uint32_t len = sizeof(struct in_cfg) - offset;
    if (len > max_len) {
        len = max_len;
    }

    memcpy(data, (uint8_t *)&bt_profiles[profile_id].in_cfg + offset, len);
    return (int32_t)len;
}

int32_t config_bt_profile_write(uint8_t profile_id, const uint8_t *data,
                                uint32_t offset, uint32_t len)
{
    if (profile_id >= BT_PROFILE_MAX || !data) {
        return -1;
    }
    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }
    if (!bt_profiles[profile_id].valid ||
        offset >= sizeof(struct in_cfg) ||
        len > sizeof(struct in_cfg) - offset) {
        return -1;
    }

    /*
     * Stage profile data in RAM only. The Web Config sends the mapping in
     * multiple BLE/ATT chunks. Persist once, after the final chunk, via
     * config_bt_profile_commit(). This avoids partial NVS writes and removes
     * the dependency on large GATT writeValue() operations.
     */
    uint32_t before=bt_profile_v30_cfg_hash(&bt_profiles[profile_id].in_cfg);
    uint32_t before_hash =
        bt_profile_v31_cfg_hash(&bt_profiles[profile_id].in_cfg);

    memcpy((uint8_t *)&bt_profiles[profile_id].in_cfg + offset, data, len);

    uint32_t after_hash =
        bt_profile_v31_cfg_hash(&bt_profiles[profile_id].in_cfg);
    uint32_t after=bt_profile_v30_cfg_hash(&bt_profiles[profile_id].in_cfg);
    bt_profile_v31_event(8, (int8_t)profile_id, 0, before_hash, after_hash);
    bt_profile_v30_trace(V30_WRITE,(int8_t)profile_id,0,0,before,after,(offset&0xffffu)|((len&0xffffu)<<16));
    return 0;
}

int32_t config_bt_profile_commit(uint8_t profile_id)
{
    if (profile_id >= BT_PROFILE_MAX) {
        return -1;
    }
    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }
    if (!bt_profiles[profile_id].valid) {
        return -1;
    }

    int32_t result=bt_profile_save_one(profile_id);
    bt_profile_v31_event(9, (int8_t)profile_id, result, 0, bt_profile_v31_cfg_hash(&bt_profiles[profile_id].in_cfg));
    bt_profile_v30_trace(V30_COMMIT,(int8_t)profile_id,bt_profiles[profile_id].source_out_idx,result,0,bt_profile_v30_cfg_hash(&bt_profiles[profile_id].in_cfg),0);
    return result;
}

uint32_t config_bt_profile_export(uint8_t *data, uint32_t max_len)
{
    const uint32_t record_size = 45;
    uint32_t offset = 1;
    uint8_t count = 0;

    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }

    if (!data || max_len < 1) {
        return 0;
    }

    memset(data, 0, max_len);

    for (uint32_t i = 0; i < BT_PROFILE_MAX; i++) {
        struct bt_profile *profile = &bt_profiles[i];
        uint8_t name_len;

        if (!profile->valid || offset + record_size > max_len) {
            continue;
        }

        data[offset++] = (uint8_t)i;
        data[offset++] = profile->valid;
        data[offset++] = profile->system_id;
        data[offset++] = profile->source_id;
        data[offset++] = profile->source_out_idx;
        data[offset++] = profile->addr_type;
        memcpy(&data[offset], profile->bdaddr, 6);
        offset += 6;

        name_len = strnlen(profile->name, sizeof(profile->name));
        data[offset++] = name_len;
        memcpy(&data[offset], profile->name, sizeof(profile->name));
        offset += sizeof(profile->name);
        count++;
    }

    data[0] = count;
    return offset;
}


uint32_t config_bt_profile_export_one(uint8_t profile_id, uint8_t *data,
                                      uint32_t max_len)
{
    const uint32_t record_size = 45;
    uint8_t name_len;

    if (!data || max_len < record_size || profile_id >= BT_PROFILE_MAX) {
        return 0;
    }
    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }
    if (!bt_profiles[profile_id].valid) {
        return 0;
    }

    memset(data, 0, record_size);
    data[0] = profile_id;
    data[1] = bt_profiles[profile_id].valid;
    data[2] = bt_profiles[profile_id].system_id;
    data[3] = bt_profiles[profile_id].source_id;
    data[4] = bt_profiles[profile_id].source_out_idx;
    data[5] = bt_profiles[profile_id].addr_type;
    memcpy(&data[6], bt_profiles[profile_id].bdaddr, 6);
    name_len = strnlen(bt_profiles[profile_id].name,
                       sizeof(bt_profiles[profile_id].name));
    data[12] = name_len;
    memcpy(&data[13], bt_profiles[profile_id].name,
           sizeof(bt_profiles[profile_id].name));
    return record_size;
}

int32_t config_bt_profile_get_count(void)
{
    int32_t count = 0;

    if (!bt_profiles_loaded) {
        config_bt_profile_init();
    }

    for (uint32_t i = 0; i < BT_PROFILE_MAX; i++) {
        if (bt_profiles[i].valid) {
            count++;
        }
    }

    return count;
}

static uint32_t config_src = DEFAULT_CFG;
static uint32_t config_version_magic[] = {
    CONFIG_MAGIC_V0,
    CONFIG_MAGIC_V1,
    CONFIG_MAGIC_V2,
    CONFIG_MAGIC_V3,
};
static uint8_t config_default_combo[BR_COMBO_CNT] = {
    PAD_LM, PAD_RM, PAD_MM, PAD_RB_UP, PAD_RB_LEFT, PAD_RB_RIGHT, PAD_RB_DOWN, PAD_LD_UP, PAD_LD_DOWN, PAD_MS
};
static bool config_rst_bare_core = false;

static void config_init_struct(struct config *data);
static void config_init_nvs_patch(struct config *data);
static int32_t config_load_from_file(struct config *data, char *filename);
static int32_t config_store_on_file(struct config *data, char *filename);
static int32_t config_v0_update(struct config *data, char *filename);
static int32_t config_v1_update(struct config *data, char *filename);
static int32_t config_v2_update(struct config *data, char *filename);

static int32_t (*config_ver_update[])(struct config *data, char *filename) = {
    config_v0_update,
    config_v1_update,
    config_v2_update,
    NULL,
};

static int32_t config_v0_update(struct config *data, char *filename) {
    memmove((uint8_t *)data + 7, (uint8_t *)data + 6, sizeof(*data) - 7);

    data->magic = CONFIG_MAGIC;
    data->global_cfg.inquiry_mode = INQ_AUTO;

    return config_store_on_file(data, filename);
}

static int32_t config_v1_update(struct config *data, char *filename) {
    memmove((uint8_t *)data + 8, (uint8_t *)data + 7, 31 - 8);

    data->magic = CONFIG_MAGIC;
    data->global_cfg.banksel = 0;

    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        printf("%s: failed to open file for reading\n", __FUNCTION__);
        goto fail;
    }
    else {
        uint32_t count = 0;
        for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
            fseek(file, 31 + (3 + 255 * 8) * i, SEEK_SET);
            count += fread((uint8_t *)&data->in_cfg[i], sizeof(struct in_cfg), 1, file);
            if (data->in_cfg[i].map_size > ADAPTER_MAPPING_MAX) {
                data->in_cfg[i].map_size = ADAPTER_MAPPING_MAX;
            }
        }
        fclose(file);

        if (count != WIRED_MAX_DEV) {
            goto fail;
        }
    }
    return config_store_on_file(data, filename);

fail:
    printf("%s: Update failed, reset config (Sorry!)\n", __FUNCTION__);
    config_init_struct(data);
    config_init_nvs_patch(data);
    return config_store_on_file(data, filename);
}

static int32_t config_get_version(uint32_t magic) {
    for (uint32_t i = 0; i < ARRAY_SIZE(config_version_magic); i++) {
        if (magic == config_version_magic[i]) {
            return i;
        }
    }
    return -1;
}

static int32_t config_v2_update(struct config *data, char *filename) {
    data->magic = CONFIG_MAGIC;

    for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
        uint32_t j = data->in_cfg[i].map_size;
        data->in_cfg[i].map_size += BR_COMBO_CNT;
        for (uint32_t k = 0; k < BR_COMBO_CNT; j++, k++) {
            data->in_cfg[i].map_cfg[j].src_btn = config_default_combo[k];
            data->in_cfg[i].map_cfg[j].dst_btn = k + BR_COMBO_BASE_1;
            data->in_cfg[i].map_cfg[j].dst_id = i;
            data->in_cfg[i].map_cfg[j].perc_max = 100;
            data->in_cfg[i].map_cfg[j].perc_threshold = 50;
            data->in_cfg[i].map_cfg[j].perc_deadzone = 135;
            data->in_cfg[i].map_cfg[j].turbo = 0;
            data->in_cfg[i].map_cfg[j].algo = 0;
        }
    }

    return config_store_on_file(data, filename);
}

static int32_t hw_config_lookup_key_name(const char* key) {
    for (uint32_t i = 0; i < sizeof(hw_config_name_idx)/sizeof(*hw_config_name_idx); i++) {
        if (strstr(key, hw_config_name_idx[i]) != NULL) {
            return i;
        }
    }
    return -1;
}

static void config_init_struct(struct config *data) {
    data->magic = CONFIG_MAGIC;
    data->global_cfg.system_cfg = WIRED_AUTO;
    data->global_cfg.multitap_cfg = MT_NONE;
    data->global_cfg.inquiry_mode = INQ_AUTO;
    data->global_cfg.banksel = 0;

    for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
        data->out_cfg[i].dev_mode = DEV_PAD;
        data->out_cfg[i].acc_mode = ACC_NONE;
        data->in_cfg[i].bt_dev_id = 0x00; /* Not used placeholder */
        data->in_cfg[i].bt_subdev_id = 0x00;  /* Not used placeholder */
        data->in_cfg[i].map_size = KBM_MAX + BR_COMBO_CNT;
        uint32_t j = 0;
        for (; j < KBM_MAX; j++) {
            data->in_cfg[i].map_cfg[j].src_btn = j;
            data->in_cfg[i].map_cfg[j].dst_btn = j;
            data->in_cfg[i].map_cfg[j].dst_id = i;
            data->in_cfg[i].map_cfg[j].perc_max = 100;
            data->in_cfg[i].map_cfg[j].perc_threshold = 50;
            data->in_cfg[i].map_cfg[j].perc_deadzone = 135;
            data->in_cfg[i].map_cfg[j].turbo = 0;
            data->in_cfg[i].map_cfg[j].algo = 0;
        }
        for (uint32_t k = 0; k < BR_COMBO_CNT; j++, k++) {
            data->in_cfg[i].map_cfg[j].src_btn = config_default_combo[k];
            data->in_cfg[i].map_cfg[j].dst_btn = k + BR_COMBO_BASE_1;
            data->in_cfg[i].map_cfg[j].dst_id = i;
            data->in_cfg[i].map_cfg[j].perc_max = 100;
            data->in_cfg[i].map_cfg[j].perc_threshold = 50;
            data->in_cfg[i].map_cfg[j].perc_deadzone = 135;
            data->in_cfg[i].map_cfg[j].turbo = 0;
            data->in_cfg[i].map_cfg[j].algo = 0;
        }
    }
}

static void config_init_nvs_patch(struct config *data) {
    esp_err_t err;
    nvs_handle_t nvs;
    uint8_t value;

    err = nvs_open("global", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        err = nvs_get_u8(nvs, "system", &value);
        if (err == ESP_OK) {
            data->global_cfg.system_cfg = value;
        }
        err = nvs_get_u8(nvs, "multitap", &value);
        if (err == ESP_OK) {
            data->global_cfg.multitap_cfg = value;
        }
        err = nvs_get_u8(nvs, "inquiry", &value);
        if (err == ESP_OK) {
            data->global_cfg.inquiry_mode = value;
        }
        err = nvs_get_u8(nvs, "bank", &value);
        if (err == ESP_OK) {
            data->global_cfg.banksel = value;
        }
        nvs_close(nvs);
    }

    err = nvs_open("output", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        err = nvs_get_u8(nvs, "mode", &value);
        if (err == ESP_OK) {
            for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
                data->out_cfg[i].dev_mode = value;
            }
        }
        err = nvs_get_u8(nvs, "accessories", &value);
        if (err == ESP_OK) {
            for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
                data->out_cfg[i].acc_mode = value;
            }
        }
        nvs_close(nvs);
    }

    err = nvs_open("mapping", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        nvs_iterator_t it = NULL;
        err = nvs_entry_find_in_handle(nvs, NVS_TYPE_ANY, &it);
        while (err == ESP_OK) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            errno = 0;
            uint32_t index = strtol(info.key, NULL, 10);
            if (!errno) {
                struct map_cfg mapping = {0};
                size_t size = sizeof(struct map_cfg);
                err = nvs_get_blob(nvs, info.key, &mapping, &size);
                if (err == ESP_OK) {
                    for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
                        memcpy(&data->in_cfg[i].map_cfg[index], &mapping, sizeof(struct map_cfg));
                        data->in_cfg[i].map_cfg[index].dst_id = i;
                    }
                }
            }
            err = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        nvs_close(nvs);
    }
}

static int32_t config_load_from_file(struct config *data, char *filename) {
#ifdef CONFIG_BLUERETRO_QEMU
    config_init_struct(data);
    config_init_nvs_patch(data);
    return 0;
#else
    struct stat st;
    int32_t ret = -1;

    if (stat(filename, &st) != 0) {
        printf("%s: No config on FS. Creating...\n", __FUNCTION__);
        config_init_struct(data);
        config_init_nvs_patch(data);
        ret = config_store_on_file(data, filename);
    }
    else {
        FILE *file = fopen(filename, "rb");
        if (file == NULL) {
            printf("%s: failed to open file for reading\n", __FUNCTION__);
        }
        else {
            uint32_t count = fread((void *)data, sizeof(*data), 1, file);
            fclose(file);
            if (count == 1) {
                ret = 0;
            }
        }
    }

    if (data->magic != CONFIG_MAGIC) {
        int32_t file_ver = config_get_version(data->magic);
        if (file_ver == -1) {
            printf("%s: Bad magic, reset config\n", __FUNCTION__);
            config_init_struct(data);
            ret = config_store_on_file(data, filename);
        }
        else {
            printf("%s: Upgrading cfg v%ld to v%d\n", __FUNCTION__, file_ver, CONFIG_VERSION);
            for (uint32_t i = file_ver; i < CONFIG_VERSION; i++) {
                if (config_ver_update[i]) {
                    ret = config_ver_update[i](data, filename);
                }
            }
        }
    }

    return ret;
#endif /* CONFIG_BLUERETRO_QEMU */
}

static int32_t config_store_on_file(struct config *data, char *filename) {
    int32_t ret = -1;

    FILE *file = fopen(filename, "wb");
    if (file == NULL) {
        printf("%s: failed to open file for writing\n", __FUNCTION__);
    }
    else {
        fwrite((void *)data, sizeof(*data), 1, file);
        fclose(file);
        ret = 0;
    }
    return ret;
}

static bool config_is_rst_required(void) {
    static uint32_t magic = 0;
    static uint8_t multitap_cfg = 0;
    static uint8_t dev_mode[WIRED_MAX_DEV] = {0};
    bool ret = false;

    if (multitap_cfg != config.global_cfg.multitap_cfg) {
        ret = true;
    }
    multitap_cfg = config.global_cfg.multitap_cfg;

    for (uint32_t i = 0; i < WIRED_MAX_DEV; i++) {
        if (dev_mode[i] != config.out_cfg[i].dev_mode) {
            ret = true;
        }
        dev_mode[i] = config.out_cfg[i].dev_mode;
    }

    if (magic != config.magic) {
        ret = false;
    }
    magic = config.magic;

    return ret;
} 

void IRAM_ATTR config_set_rst_bare_core(bool value) {
    config_rst_bare_core = value;
}

void hw_config_patch(void) {
    esp_err_t err;
    nvs_handle_t nvs;

    err = nvs_open("hw", NVS_READONLY, &nvs);
    if (err == ESP_OK) {
        nvs_iterator_t it = NULL;
        err = nvs_entry_find_in_handle(nvs, NVS_TYPE_ANY, &it);
        while (err == ESP_OK) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            int32_t index = hw_config_lookup_key_name(info.key);
            if (index > -1) {
                uint32_t value;
                err = nvs_get_u32(nvs, info.key, &value);
                if (err == ESP_OK) {
                    hw_config.data32[index] = value;
                }
            }
            err = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        nvs_close(nvs);
    }
}

void config_init(uint32_t src) {
    config_bt_profile_init();

    char tmp_str[32] = "/fs/";
    char *filename = CONFIG_FILE;
    char *gameid = gid_get();
    config_src = DEFAULT_CFG;

    if (src == GAMEID_CFG && strlen(gameid)) {
        struct stat st;

        strcat(tmp_str, gameid);
        if (stat(tmp_str, &st) == 0) {
            filename = tmp_str;
            config_src = GAMEID_CFG;
        }
    }

    uint32_t profile_table_before_config_init = bt_profile_v30_table_hash();
    config_load_from_file(&config, filename);

    bt_profile_v31_event(
        10,
        -1,
        0,
        profile_table_before_config_init,
        bt_profile_v30_table_hash());

    bt_profile_v30_trace(V30_CONFIG_INIT,-1,0,0,0,bt_profile_v30_cfg_hash(&config.in_cfg[0]),(uint32_t)config_src);
    if (config_rst_bare_core && config_is_rst_required()) {
        sys_mgr_cmd(SYS_MGR_CMD_WIRED_RST);
        printf("# %s: Reloaded wired core cfg: %s\n", __FUNCTION__, filename);
    }
}

void config_update(uint32_t dst) {
    char tmp_str[32] = "/fs/";
    char *filename = CONFIG_FILE;
    char *gameid = gid_get();
    config_src = DEFAULT_CFG;

    if (dst == GAMEID_CFG && strlen(gameid)) {
        strcat(tmp_str, gameid);
        filename = tmp_str;
        config_src = GAMEID_CFG;
    }

    /* Global/console/game saves are independent of controller profiles.
     * A controller profile is modified only through its explicit profile write. */
    uint32_t before=bt_profile_v30_cfg_hash(&config.in_cfg[0]);
    uint32_t profile_table_before_update = bt_profile_v30_table_hash();
    config_store_on_file(&config, filename);

    bt_profile_v31_event(
        11,
        -1,
        0,
        profile_table_before_update,
        bt_profile_v30_table_hash());

    bt_profile_v30_trace(V30_CONFIG_UPDATE,-1,0,0,before,bt_profile_v30_cfg_hash(&config.in_cfg[0]),(uint32_t)config_src);
    if (config_rst_bare_core && config_is_rst_required()) {
        sys_mgr_cmd(SYS_MGR_CMD_WIRED_RST);
        printf("# %s: Reloaded wired core cfg: %s\n", __FUNCTION__, filename);
    }
}

uint32_t config_get_src(void) {
    return config_src;
}

void config_debug_log(void) {
        bt_mon_log(true,
            "Global config: system: 0x%02X multitap: 0x%02X inquiry: 0x%02X banksel: 0x%02X",
            config.global_cfg.system_cfg, config.global_cfg.multitap_cfg,
            config.global_cfg.inquiry_mode, config.global_cfg.banksel);
        bt_mon_log(true, "Output config #0: device_mode: 0x%02X acc_mode: 0x%02X",
            config.out_cfg[0].dev_mode, config.out_cfg[0].acc_mode);
        bt_mon_log(true, "Mapping config #0");
        bt_mon_log(true, "Bluetooth mapping profiles: %ld", config_bt_profile_get_count());
        bt_mon_tx(BT_MON_SYS_NOTE, (uint8_t *)config.in_cfg[0].map_cfg,
            config.in_cfg[0].map_size * sizeof(config.in_cfg[0].map_cfg[0]));
}

#ifndef _CTRL_COM_EMS__H
#define _CTRL_COM_EMS__H

#include <stddef.h>
#include <unistd.h>

#include "ctrl/logger.h"
#include "ctrl/com/mqtt.h"

#define SIZE_TEST(NAME,TYPE,SIZE) struct size_test_##NAME { int i[(sizeof(TYPE)== SIZE) * 2 - 1]; }

#define OFFSET_TEST(NAME,TYPE,MEMBER,OFFS) struct offs_test_##NAME { int i[(offsetof(TYPE, MEMBER) == OFFS) * 2 - 1]; }

enum ems_tel_type
{
  ETT_UBA_MON_FAST = 0x18,
  ETT_UBA_MON_SLOW = 0x19,
  ETT_UBA_MON_WWM = 0x34,
  ETT_EMSPLUS = 0xFF
}__attribute__((__packed__));

SIZE_TEST(enum_ems_tel_type, enum ems_tel_type, 1);

struct ems_uba_component_state
{
  uint8_t  gas:1;
  uint8_t  res1:1;
  uint8_t  blower:1;
  uint8_t  ignite:1;
  uint8_t  res2:1;
  uint8_t  pump:1;
  uint8_t  valve:1;
  uint8_t  circ:1;
} __attribute__((packed));

SIZE_TEST(struct_ems_uba_component_state, struct ems_uba_component_state, 1);


struct ems_uba_temperatures
{
  int16_t  tmp1;
  int16_t  water;
  int16_t  rl;
} __attribute__((packed));

SIZE_TEST(struct_ems_uba_temperatures, struct ems_uba_temperatures, 6);


struct ems_uba_monitor_fast
{
  uint8_t  vl_soll;
  int16_t  vl_ist;
  uint8_t  ks_max_p;
  uint8_t  ks_akt_p;
  uint16_t res0;
  struct ems_uba_component_state on;
  uint8_t res1;
  struct ems_uba_temperatures tmp;
  uint16_t fl_current;
  uint8_t  sys_press;
  char service_code[2];
  uint16_t err;
  uint8_t res2[3];
} __attribute__((packed));

SIZE_TEST(struct_ems_uba_monitor_fast, struct ems_uba_monitor_fast, 25);

enum ems_device {
  EMS_DEV_BOILER     = 0x08,
  EMS_DEV_THERMOSTAT = 0x10,
  EMS_DEV_SELF       = 0x0B
};

enum ems_uba_mon_wwm_type
{
  EUMW_TYPE_NONE           = 0x00,
  EUMW_TYPE_BOILER         = 0x01,
  EUMW_TYPE_STORAGE_BOILER = 0x02,
  EUMW_TYPE_STORAGE        = 0x03
} __attribute__((packed));

struct ems_uba_mon_wwm_sw1
{
  uint8_t daylight_mode:1;
  uint8_t single_load:1;
  uint8_t desinfect:1;
  uint8_t active:1;
  uint8_t reloading:1;
  uint8_t temp_ok:1;
  uint8_t res:2;
} __attribute__((packed));

struct ems_uba_mon_wwm_fails
{
  uint8_t probe_1:1;
  uint8_t probe_2:1;
  uint8_t ww:1;
  uint8_t desinfect:1;
  uint8_t res:4;
} __attribute__((packed));


struct ems_uba_mon_wwm_sw2
{
  uint8_t circ_daylight:1;
  uint8_t circ_manual:1;
  uint8_t circ_active:1;
  uint8_t is_loading:1;
  uint8_t res:4;
} __attribute__((packed));



struct ems_uba_monitor_wwm
{
  int8_t  soll;
  int16_t ist[2];

  struct ems_uba_mon_wwm_sw1 sw1;

  struct ems_uba_mon_wwm_fails fail;

  struct ems_uba_mon_wwm_sw2 sw2;

  enum ems_uba_mon_wwm_type type;
  uint8_t throughput;
  uint8_t op_time[3];
  uint8_t op_count[3];

  uint32_t op_time_sane;
  uint32_t op_count_sane;
} __attribute__((packed));

SIZE_TEST(struct_ems_uba_monitor_wwm, struct ems_uba_monitor_wwm, 24);

struct ems_uba_monitor_slow
{
  int16_t tmp_out;
  int16_t tmp_boiler;
  int16_t tmp_exhaust;
  uint8_t res1[3];
  uint8_t pump_mod;
  uint8_t burner_starts[3];
  uint8_t run_time[3];
  uint8_t run_time_stage_2[3];
  uint8_t run_time_heating[3];
  uint8_t time_res[3];

  uint32_t burner_starts_sane;
  uint32_t run_time_sane;
  uint32_t run_time_stage_2_sane;
  uint32_t run_time_heating_sane;

} __attribute__((packed));

SIZE_TEST(struct_ems_uba_monitor_slow, struct ems_uba_monitor_slow, 41);

struct ems_plus_t01a5
{
  int16_t room_temp_act;      // 0.1°
  uint8_t winter_mode;
  uint8_t room_temp_nom;      // 0.5°
  uint8_t vl_temp_nom;        // 1.0°
  uint8_t res0;
  uint8_t prg_room_temp_act;  // 0.5°
  uint8_t prg_room_temp_nom;  // 0.5°
  uint16_t mode_remain_time;  // 1 min
  uint8_t mode_manual:1;
  uint8_t mode_heat_hilo:1;
  uint8_t res1:6;
  uint8_t prg_mode_manual:1;
  uint8_t prg_mode_heat_hilo:1;
  uint8_t res2:6;
  uint8_t next_mode_manual:1;
  uint8_t next_mode_heat_hilo:1;
  uint8_t res3:6;
  uint16_t prg_mode_remain_time;  // 1 min
  uint16_t prg_mode_passed_time;  // 1 min
  uint8_t res4[7];
  uint8_t extension[16];
} __attribute__((packed));

union ems_plus_payload
{
  uint8_t raw[1];
  struct ems_plus_t01a5 t01a5;
};

enum emsplus_type
{
  EMSPLUS_01A5 = 0x01A5
} __attribute__((packed));


struct ems_plus_data
{
  enum emsplus_type type;
  union ems_plus_payload d;
} __attribute__((packed));


union ems_telegram_data
{
  uint8_t raw[1];
  struct ems_uba_monitor_fast uba_mon_fast;
  struct ems_uba_monitor_slow uba_mon_slow;
  struct ems_uba_monitor_wwm  uba_mon_wwm;
  struct ems_plus_data emsplus;
};

struct ems_telegram_head
{
  uint8_t src;
  uint8_t dst;
  enum ems_tel_type type;
  uint8_t offs;
}__attribute__((packed));

SIZE_TEST(struct_ems_telegram_head, struct ems_telegram_head, 4);


struct ems_telegram
{
  struct ems_telegram_head h;
  union ems_telegram_data d;
}__attribute__((packed));


OFFSET_TEST(ems_telegram_uba_tmp_rl,   struct ems_telegram, d.uba_mon_fast.tmp.rl,    17);
OFFSET_TEST(ems_telegram_uba_tmp_water,struct ems_telegram, d.uba_mon_fast.tmp.water, 15);
OFFSET_TEST(ems_telegram_uba_err,      struct ems_telegram, d.uba_mon_fast.err,       24);
OFFSET_TEST(ems_telegram_uba_ks_akt_p, struct ems_telegram, d.uba_mon_fast.ks_akt_p,   8);

typedef void (*logic_cb)(int val_id);

struct ems_logic_fct_val
{
  enum ems_tel_type tel_type;
  uint16_t          offs;
  uint16_t          len;
};

struct ems_logic_cb {
  logic_cb                         cb;
  const struct ems_logic_fct_val * test_val;
};

void ems_init(struct mqtt_handle * mqtt);
void ems_copy_telegram(struct ems_telegram * tel, size_t len);
void ems_log_telegram(struct ems_telegram * tel, size_t len);
void ems_publish_telegram(struct ems_telegram * tel, size_t len);
void ems_logic_evaluate_telegram(struct ems_telegram * tel, size_t len);

void ems_switch_circ(enum ems_device dev, int state);

void print_telegram(int out, enum log_level loglevel, const char * prefix, uint8_t * msg, size_t len);

#endif  //_CTRL_COM_EMS__H

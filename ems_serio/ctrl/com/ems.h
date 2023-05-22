#ifndef _CTRL_COM_EMS__H
#define _CTRL_COM_EMS__H

#include <stddef.h>
#include <unistd.h>

#include "tool/logger.h"
#include "ctrl/com/mqtt.h"


#define ONOFF(VALUE) ((VALUE) ? "ON " : "OFF")
#define NANVAL(VALUE) ((VALUE) == 0x8000 ? (0.0 / 0.0) : 0.1f * (VALUE))
#define NANVA8(VALUE) ((VALUE) == 0xFF ? (0.0 / 0.0) : 0.1f * (VALUE))
#define TRIVAL(VALUE) ((VALUE)[2] + ((VALUE)[1] << 8) + ((VALUE)[0] << 16))


#define SIZE_TEST(NAME,TYPE,SIZE)   \
  struct size_test_##NAME {          \
    int min[sizeof(TYPE) - SIZE];     \
    int max[SIZE - sizeof(TYPE)];  }


#define OFFSET_TEST(NAME,TYPE,MEMBER,OFFS)    \
    struct offs_test_##NAME {                  \
      int min[offsetof(TYPE, MEMBER) - OFFS];   \
      int max[OFFS - offsetof(TYPE, MEMBER)]; }


enum ems_tel_type
{
  ETT_UBA_MON_FAST = 0x18,
  ETT_UBA_MON_SLOW = 0x19,
  ETT_UBA_MON_WWM = 0x34,
  ETT_EMS_2_TELEGRAM = 0xF0
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
  uint16_t  tmp1;
  uint16_t  water;
  uint16_t  rl;
} __attribute__((packed));

SIZE_TEST(struct_ems_uba_temperatures, struct ems_uba_temperatures, 6);


struct ems_uba_monitor_fast
{
  uint8_t vl_soll;
  uint16_t vl_ist;
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


enum ems_uba_mon_wwm_type
{
  EUMW_TYPE_NONE           = 0x00,
  EUMW_TYPE_BOILER         = 0x01,
  EUMW_TYPE_STORAGE_BOILER = 0x02,
  EUMW_TYPE_STORAGE        = 0x03
} __attribute__((packed));

struct ems_uba_monitor_wwm
{
  uint8_t  soll;
  uint16_t ist[2];

  uint8_t daylight_mode:1;
  uint8_t single_load:1;
  uint8_t desinfect:1;
  uint8_t active:1;
  uint8_t reloading:1;
  uint8_t temp_ok:1;
  uint8_t res1:2;

  uint8_t fail_probe_1:1;
  uint8_t fail_probe_2:1;
  uint8_t fail_ww:1;
  uint8_t fail_desinfect:1;
  uint8_t res2:4;

  uint8_t circ_daylight:1;
  uint8_t circ_manual:1;
  uint8_t circ_active:1;
  uint8_t is_loading:1;
  uint8_t res3:4;

  enum ems_uba_mon_wwm_type type;
  uint8_t throughput;
  uint8_t op_time[3];
  uint8_t op_count[3];
} __attribute__((packed));

SIZE_TEST(struct_ems_uba_monitor_wwm, struct ems_uba_monitor_wwm, 16);

struct ems_uba_monitor_slow
{
  uint16_t tmp_out;
  uint16_t tmp_boiler;
  uint16_t tmp_exhaust;
  uint8_t res1[3];
  uint8_t pump_mod;
  uint8_t burner_starts[3];
  uint8_t run_time[3];
  uint8_t run_time_stage_2[3];
  uint8_t run_time_heating[3];
  uint8_t time_res[3];
} __attribute__((packed));

SIZE_TEST(struct_ems_uba_monitor_slow, struct ems_uba_monitor_slow, 25);


union ems_telegram_data
{
  uint8_t raw[0];
  struct ems_uba_monitor_fast uba_mon_fast;
  struct ems_uba_monitor_slow uba_mon_slow;
  struct ems_uba_monitor_wwm  uba_mon_wwm;
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




void ems_swap_telegram(struct ems_telegram * tel);
void ems_log_telegram(enum log_level ll, struct ems_telegram * tel, size_t rx_len);
void ems_publish_telegram(struct mqtt_handle * mqtt, struct ems_telegram * tel);


#endif  //_CTRL_COM_EMS__H
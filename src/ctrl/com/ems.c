#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#include "ctrl/com/ems.h"
#include "io/tx.h"
#include "tools/msg_queue.h"
#include "ctrl/logger.h"
#include "ctrl/com/mqtt.h"
#include "ctrl/com/logic.h"
#include "stringhelp.h"

#define ONOFF(VALUE) ((VALUE) ? "ON " : "OFF")
#define NANVAL(VALUE) ((VALUE) == 0x8000 ? (0.0 / 0.0) : 0.1f * (VALUE))
#define NANVA8(VALUE) ((VALUE) == 0xFF ? (0.0 / 0.0) : 0.1f * (VALUE))
#define TRIVAL(VALUE) ((VALUE)[2] + ((VALUE)[1] << 8) + ((VALUE)[0] << 16))

struct ems_plus_t01a5       emsplus_t01a5;
struct ems_uba_monitor_fast uba_mon_fast;
struct ems_uba_monitor_slow uba_mon_slow;
struct ems_uba_monitor_wwm  uba_mon_wwm;

struct mqtt_handle        * mqtt  = NULL;
const struct ems_logic_cb * logic = NULL;

struct entity_params
{
  size_t   offs;
  size_t   size;
  uint32_t mask;
  const char * fmt;
  const char * type;
  const char * entity;
  time_t       last_publish;
};


struct entity_params uba_mon_fast_params[] =
{
  { offsetof(struct ems_uba_monitor_fast, tmp.water   ), sizeof(uba_mon_fast.tmp.water   ), 0xFFFFFFFF, "%d",   "sensor", "uba_water"        , 0},
  { offsetof(struct ems_uba_monitor_fast, vl_ist      ), sizeof(uba_mon_fast.vl_ist      ), 0xFFFFFFFF, "%d",   "sensor", "uba_vl_act"       , 0},
  { offsetof(struct ems_uba_monitor_fast, vl_soll     ), sizeof(uba_mon_fast.vl_soll     ), 0xFFFFFFFF, "%u",   "sensor", "uba_vl_nom"       , 0},
  { offsetof(struct ems_uba_monitor_fast, on          ), sizeof(uba_mon_fast.on          ), 0x00000020, "%u",   "relay" , "uba_on_pump"      , 0},
  { offsetof(struct ems_uba_monitor_fast, on          ), sizeof(uba_mon_fast.on          ), 0x00000001, "%u",   "relay" , "uba_on_gas"       , 0},
  { offsetof(struct ems_uba_monitor_fast, on          ), sizeof(uba_mon_fast.on          ), 0x00000040, "%u",   "relay" , "uba_on_valve"     , 0},
  { offsetof(struct ems_uba_monitor_fast, on          ), sizeof(uba_mon_fast.on          ), 0x00000004, "%u",   "relay" , "uba_on_blower"    , 0},
  { offsetof(struct ems_uba_monitor_fast, on          ), sizeof(uba_mon_fast.on          ), 0x00000080, "%u",   "relay" , "uba_on_circ"      , 0},
  { offsetof(struct ems_uba_monitor_fast, on          ), sizeof(uba_mon_fast.on          ), 0x00000008, "%u",   "relay" , "uba_on_igniter"   , 0},
  { offsetof(struct ems_uba_monitor_fast, ks_akt_p    ), sizeof(uba_mon_fast.ks_akt_p    ), 0xFFFFFFFF, "%u",   "sensor", "uba_power_act"    , 0},
  { offsetof(struct ems_uba_monitor_fast, ks_max_p    ), sizeof(uba_mon_fast.ks_max_p    ), 0xFFFFFFFF, "%u",   "sensor", "uba_power_max"    , 0},
  { offsetof(struct ems_uba_monitor_fast, fl_current  ), sizeof(uba_mon_fast.fl_current  ), 0xFFFFFFFF, "%u",   "sensor", "uba_flame_current", 0},
  { offsetof(struct ems_uba_monitor_fast, err         ), sizeof(uba_mon_fast.err         ), 0xFFFFFFFF, "%u",   "sensor", "uba_error"        , 0},
  { offsetof(struct ems_uba_monitor_fast, service_code), sizeof(uba_mon_fast.service_code), 0xFFFFFFFF, "%.2s", "sensor", "uba_service_code" , 0}
};

struct entity_params uba_mon_slow_params[] =
{
  { offsetof(struct ems_uba_monitor_slow, tmp_out)              , sizeof(uba_mon_slow.tmp_out)              , 0xFFFFFFFF, "%d",   "sensor", "uba_water"     , 0},
  { offsetof(struct ems_uba_monitor_slow, run_time)             , sizeof(uba_mon_slow.run_time)             , 0xFFFFFFFF, "%d",   "sensor", "uba_vl_act"    , 0},
  { offsetof(struct ems_uba_monitor_slow, pump_mod)             , sizeof(uba_mon_slow.pump_mod)             , 0xFFFFFFFF, "%u",   "sensor", "uba_vl_nom"    , 0},
  { offsetof(struct ems_uba_monitor_slow, run_time_heating_sane), sizeof(uba_mon_slow.run_time_heating_sane), 0xFFFFFFFF, "%u",   "relay" , "uba_on_pump"   , 0},
  { offsetof(struct ems_uba_monitor_slow, run_time_stage_2_sane), sizeof(uba_mon_slow.run_time_stage_2_sane), 0xFFFFFFFF, "%u",   "relay" , "uba_on_gas"    , 0},
  { offsetof(struct ems_uba_monitor_slow, burner_starts_sane   ), sizeof(uba_mon_slow.burner_starts_sane)   , 0xFFFFFFFF, "%u",   "relay" , "uba_on_valve"  , 0}
};

#define CHECK_MSG_SIZE(MSGTYPE,MSGLEN,OFFS,LEN) (((MSGLEN) < (OFFS) + (LEN)) ? LG_INFO(#MSGTYPE" Msg with overflowing len/offs vs expected length: 0x%02X/0x%02X vs 0x%02X.", LEN, OFFS, MSGLEN), FALSE : TRUE)

#define SWAP_TEL_S(MSG,MEMBER,OFFS,LEN) { if (offsetof(typeof(MSG),MEMBER) >= (OFFS) && offsetof(typeof(MSG),MEMBER) + sizeof((MSG).MEMBER) - 1 <= ((OFFS) + (LEN))) { (MSG).MEMBER = ntohs((MSG).MEMBER); } }

#define CHECK_NUM_UPDATE(VALOFFS,VALLEN,MSGOFFS,MSGLEN) ((VALOFFS) >= (MSGOFFS) && (VALOFFS) + (VALLEN) <= ((MSGOFFS) + (MSGLEN) + 1))
#define CHECK_UPDATE(MSG,MEMBER,OFFS,LEN) (CHECK_NUM_UPDATE(offsetof(typeof(MSG),MEMBER),sizeof((MSG).MEMBER),OFFS,LEN))

#define NTOHU_TRIVAL(VALUE) (VALUE[0] + (VALUE[1] << 8) + (VALUE[2] << 16))
#define NTOH_TRIVAL(VALUE)  (VALUE[0] + (VALUE[1] << 8) + (VALUE[2] << 16) + ((VALUE[2] >> 7) * 0xFF000000UL))

void ems_init(struct mqtt_handle * mqtt_handle) {
  mqtt = mqtt_handle;
  logic_init(&logic);
}

void ems_copy_telegram(struct ems_telegram * tel, size_t len)
{
  len -= 5;
  switch (tel->h.type)
  {
    case ETT_EMSPLUS:
    {
      switch (ntohs(tel->d.emsplus.type))
      {
        case EMSPLUS_01A5:
          if (CHECK_MSG_SIZE(emsplus_t01a5, sizeof(struct ems_plus_t01a5), tel->h.offs, len - sizeof(tel->d.emsplus.type))) {
            memcpy(((uint8_t *) &emsplus_t01a5) + tel->h.offs, tel->d.emsplus.d.raw, len);
            SWAP_TEL_S(emsplus_t01a5, room_temp_act, tel->h.offs, len);
            SWAP_TEL_S(emsplus_t01a5, mode_remain_time, tel->h.offs, len);
            SWAP_TEL_S(emsplus_t01a5, prg_mode_remain_time, tel->h.offs, len);
            SWAP_TEL_S(emsplus_t01a5, prg_mode_passed_time, tel->h.offs, len);
          } else {
            print_telegram(0, LL_INFO, "Content: ", (uint8_t *) tel, len);
          }
        break;
        default: break;
      }
    }
    break;
    case ETT_UBA_MON_FAST:
    {
      if (CHECK_MSG_SIZE(uba_mon_fast, sizeof(struct ems_uba_monitor_fast), tel->h.offs, len)) {
        memcpy(((uint8_t *) &uba_mon_fast) + tel->h.offs, tel->d.raw, len);
        SWAP_TEL_S(uba_mon_fast, err, tel->h.offs, len);
        SWAP_TEL_S(uba_mon_fast, fl_current, tel->h.offs, len);
        SWAP_TEL_S(uba_mon_fast, tmp.rl, tel->h.offs, len);
        SWAP_TEL_S(uba_mon_fast, tmp.tmp1, tel->h.offs, len);
        SWAP_TEL_S(uba_mon_fast, tmp.water, tel->h.offs, len);
        SWAP_TEL_S(uba_mon_fast, vl_ist, tel->h.offs, len);
      } else {
        print_telegram(0, LL_INFO, "Content: ", (uint8_t *) tel, len);
      }
    }
    break;
    case ETT_UBA_MON_SLOW:
    {
      if (CHECK_MSG_SIZE(uba_mon_slow, sizeof(struct ems_uba_monitor_slow), tel->h.offs, len)) {
        memcpy(((uint8_t *) &uba_mon_slow) + tel->h.offs, tel->d.raw, len);
        SWAP_TEL_S(uba_mon_slow, tmp_boiler, tel->h.offs, len);
        SWAP_TEL_S(uba_mon_slow, tmp_exhaust, tel->h.offs, len);
        SWAP_TEL_S(uba_mon_slow, tmp_out, tel->h.offs, len);
        uba_mon_slow.burner_starts_sane = NTOHU_TRIVAL(uba_mon_slow.burner_starts);
        uba_mon_slow.run_time_heating_sane = NTOH_TRIVAL(uba_mon_slow.run_time_heating);
        uba_mon_slow.run_time_sane = NTOH_TRIVAL(uba_mon_slow.run_time);
        uba_mon_slow.run_time_stage_2_sane = NTOH_TRIVAL(uba_mon_slow.run_time_stage_2);
      } else {
        print_telegram(0, LL_INFO, "Content: ", (uint8_t *) tel, len);
      }
    }
    break;
    case ETT_UBA_MON_WWM:
      if (CHECK_MSG_SIZE(uba_mon_wwm, sizeof(struct ems_uba_monitor_wwm), tel->h.offs, len)) {
        memcpy(((uint8_t *) &uba_mon_wwm) + tel->h.offs, tel->d.raw, len);
        SWAP_TEL_S(uba_mon_wwm, ist[0], tel->h.offs, len);
        SWAP_TEL_S(uba_mon_wwm, ist[1], tel->h.offs, len);
        uba_mon_wwm.op_time_sane = NTOHU_TRIVAL(uba_mon_wwm.op_time);
        uba_mon_wwm.op_count_sane = NTOHU_TRIVAL(uba_mon_wwm.op_count);
      } else {
        print_telegram(0, LL_INFO, "Content: ", (uint8_t *) tel, len);
      }

    break;
    default: break;
  }
}


void ems_log_telegram(struct ems_telegram * tel, size_t len)
{
  switch (tel->h.type)
  {
    case ETT_EMSPLUS:
    {
      uint16_t ems_plus_type = ntohs(tel->d.emsplus.type);
      switch (ems_plus_type)
      {
        case EMSPLUS_01A5:
          print_telegram(1, LL_DEBUG_MORE, "EMS+ Telegram 01a5", (uint8_t *) tel, len);
          LG_DEBUG("CW400 - Room Temp  %04.1f°C.", NANVAL(emsplus_t01a5.room_temp_act));
        break;
        default:
          LG_INFO("Unknown EMS+ Telegram Type 0x%04X", ems_plus_type);
          print_telegram(0, LL_INFO, "Content:", (uint8_t *) tel, len);
        break;
      }
    }
    break;
    case ETT_UBA_MON_FAST:
    {
      struct ems_uba_monitor_fast * msg = &uba_mon_fast;
      print_telegram(1, LL_DEBUG_MORE, "EMS Telegram UBA_mon_fast", (uint8_t *) tel, len);

      LG_DEBUG("Mon fast   VL: %04.1f°C/%04.1f°C (ist/soll)  RL: %04.1f °C     WW: %04.1f °C     Tmp?: %04.1f °C."
          , 0.1f * msg->vl_ist, 1.0f * msg->vl_soll, NANVAL(msg->tmp.rl), NANVAL(msg->tmp.water), NANVAL(msg->tmp.tmp1 ));
      LG_DEBUG("  len: %2u  Pump: %s       Blow: %s       Gas: %s      Ignite: %s      Circ: %s       Valve: %s.", len
                ,ONOFF(msg->on.pump), ONOFF(msg->on.blower), ONOFF(msg->on.gas    ), ONOFF(msg->on.ignite ), ONOFF(msg->on.circ   ), ONOFF(msg->on.valve  ));
      LG_DEBUG("  src: %02X  KsP: %03d%%/%03d%% (akt/max)    FlCurr: %04.1f µA Druck: %04.1f bar.", tel->h.src
                , msg->ks_akt_p, msg->ks_max_p, NANVAL(msg->fl_current), NANVA8(msg->sys_press));

      LG_DEBUG("  dst: %02X  Error: %d  ServiceCode: %.2s.", tel->h.dst, msg->err, msg->service_code);
    }
    break;
    case ETT_UBA_MON_SLOW:
    {
      struct ems_uba_monitor_slow * msg = &uba_mon_slow;

      print_telegram(1, LL_DEBUG_MORE, "EMS Telegram UBA_Mon_slow", (uint8_t *) tel, len);
      LG_DEBUG("Mon slow  Out: %04.1f °C    boiler: %04.1f °C    exhaust: %04.1f °C     Pump Mod: %3u %%"
          , NANVAL(msg->tmp_out), NANVAL(msg->tmp_boiler), NANVAL(msg->tmp_exhaust), msg->pump_mod);
      LG_DEBUG("  len: %2u       Burner starts: % 8d       runtime: % 8d min    rt-stage2: % 8d min      rt-heating: % 8d min.", len
                ,TRIVAL(msg->burner_starts), TRIVAL(msg->run_time), TRIVAL(msg->run_time_stage_2), TRIVAL(msg->run_time_heating));
      LG_DEBUG("  src: %02X     dst: %02X.", tel->h.src, tel->h.dst);
    }
    break;
    case ETT_UBA_MON_WWM:
    {
      struct ems_uba_monitor_wwm * msg = &uba_mon_wwm;

      print_telegram(1, LL_DEBUG_MORE, "EMS Telegram UBA_Mon_wwm", (uint8_t *) tel, len);
      LG_DEBUG("Mon WWM   Ist: %04.1f°C/%04.1f°C  Soll: %04.1f °C     Lädt: %s      Durchfluss: %04.1f l/m."
          , NANVAL(msg->ist[0]), NANVAL(msg->ist[1]), 1.0f * msg->soll, ONOFF(msg->sw2.is_loading), NANVA8(msg->throughput));
      LG_DEBUG("  len: %2u Circ: %s       Man: %s       Day: %s      Single: %s      Day: %s       Reload: %s.", len
                ,ONOFF(msg->sw2.circ_active), ONOFF(msg->sw2.circ_manual), ONOFF(msg->sw2.circ_daylight), ONOFF(msg->sw1.single_load ), ONOFF(msg->sw1.daylight_mode), ONOFF(msg->sw1.reloading));
      LG_DEBUG("  src: %02X Type: %02X    prod: %s    Detox: %s     count: %8u   time: %8u min   temp: %s.", tel->h.src
                , msg->type, ONOFF(msg->sw1.active), ONOFF(msg->sw1.desinfect), TRIVAL(msg->op_count), TRIVAL(msg->op_time), msg->sw1.temp_ok ? " OK" : "NOK");
      LG_DEBUG("  dst: %02X WWErr: %s    PR1Err: %s   PR2Err: %s   DesErr: %s.", tel->h.dst
               , ONOFF(msg->fail.ww), ONOFF(msg->fail.probe_1), ONOFF(msg->fail.probe_2), ONOFF(msg->fail.desinfect));
    }
    break;
    default:
      LG_INFO("Unknown EMS Telegram Type 0x%02X", tel->h.type);
      print_telegram(0, LL_INFO, "Content: ", (uint8_t *) tel, len);
    break;
  }
}

void ems_publish_telegram(struct ems_telegram * tel, size_t len)
{
  len -= 5;
  switch (tel->h.type)
  {
    case ETT_EMSPLUS:
    {
      switch (ntohs(tel->d.emsplus.type))
      {
        case EMSPLUS_01A5:
          if (CHECK_UPDATE(emsplus_t01a5, room_temp_act, tel->h.offs, len))  mqtt_publish(mqtt, "sensor", "CW400_room_temp", emsplus_t01a5.room_temp_act);
        break;
        default: break;
      }
    }
    break;
    case ETT_UBA_MON_FAST:
      if (CHECK_UPDATE(uba_mon_fast, tmp.water, tel->h.offs, len))  mqtt_publish(mqtt, "sensor", "uba_water", uba_mon_fast.tmp.water);
      if (CHECK_UPDATE(uba_mon_fast, vl_ist, tel->h.offs, len))     mqtt_publish(mqtt, "sensor", "uba_vl_act", uba_mon_fast.vl_ist);
      if (CHECK_UPDATE(uba_mon_fast, vl_soll, tel->h.offs, len))    mqtt_publish(mqtt, "sensor", "uba_vl_nom", uba_mon_fast.vl_soll);
      if (CHECK_UPDATE(uba_mon_fast, on, tel->h.offs, len)) {
        mqtt_publish(mqtt, "relay", "uba_on_pump", uba_mon_fast.on.pump);
        mqtt_publish(mqtt, "relay", "uba_on_gas", uba_mon_fast.on.gas);
        mqtt_publish(mqtt, "relay", "uba_on_valve", uba_mon_fast.on.valve);
        mqtt_publish(mqtt, "relay", "uba_on_blower", uba_mon_fast.on.blower);
        mqtt_publish(mqtt, "relay", "uba_on_circ", uba_mon_fast.on.circ);
        mqtt_publish(mqtt, "relay", "uba_on_igniter", uba_mon_fast.on.ignite);
      }
      if (CHECK_UPDATE(uba_mon_fast, ks_akt_p, tel->h.offs, len))     mqtt_publish(mqtt, "sensor", "uba_power_act", uba_mon_fast.ks_akt_p);
      if (CHECK_UPDATE(uba_mon_fast, ks_max_p, tel->h.offs, len))     mqtt_publish(mqtt, "sensor", "uba_power_max", uba_mon_fast.ks_max_p);
      if (CHECK_UPDATE(uba_mon_fast, fl_current, tel->h.offs, len))   mqtt_publish(mqtt, "sensor", "uba_flame_current", uba_mon_fast.fl_current);
      if (CHECK_UPDATE(uba_mon_fast, err, tel->h.offs, len))          mqtt_publish(mqtt, "sensor", "uba_error", uba_mon_fast.err);
      if (CHECK_UPDATE(uba_mon_fast, service_code, tel->h.offs, len)) mqtt_publish_formatted(mqtt, "sensor", "uba_service_code", "%.2s", uba_mon_fast.service_code);
    break;
    case ETT_UBA_MON_SLOW:
      if (CHECK_UPDATE(uba_mon_slow, tmp_out, tel->h.offs, len))   mqtt_publish(mqtt, "sensor", "uba_outside", uba_mon_slow.tmp_out);
      if (CHECK_UPDATE(uba_mon_slow, pump_mod, tel->h.offs, len))  mqtt_publish(mqtt, "sensor", "uba_pump_mod", uba_mon_slow.pump_mod);
/*
      if (CHECK_UPDATE(uba_mon_slow, run_time_heating, tel->h.offs, len)) mqtt_publish(mqtt, "sensor", "uba_rt_heating", uba_mon_slow.run_time_heating_sane);
      if (CHECK_UPDATE(uba_mon_slow, run_time, tel->h.offs, len)) mqtt_publish(mqtt, "sensor", "uba_rt_boiler", uba_mon_slow.run_time_sane);
      if (CHECK_UPDATE(uba_mon_slow, run_time_stage_2, tel->h.offs, len)) mqtt_publish(mqtt, "sensor", "uba_rt_boiler_2", uba_mon_slow.run_time_stage_2_sane);
      if (CHECK_UPDATE(uba_mon_slow, burner_starts, tel->h.offs, len)) mqtt_publish(mqtt, "sensor", "uba_burner_starts", uba_mon_slow.burner_starts_sane);
*/
    break;
    case ETT_UBA_MON_WWM:
      if (CHECK_UPDATE(uba_mon_wwm, ist[0], tel->h.offs, len)) mqtt_publish(mqtt, "sensor", "uba_ww_act1", uba_mon_wwm.ist[0]);
      if (CHECK_UPDATE(uba_mon_wwm, soll, tel->h.offs, len))   mqtt_publish(mqtt, "sensor", "uba_ww_nom", uba_mon_wwm.soll);
      if (CHECK_UPDATE(uba_mon_wwm, sw2, tel->h.offs, len)) {
        mqtt_publish(mqtt, "relay", "uba_ww_circ_active", uba_mon_wwm.sw2.circ_active);
        mqtt_publish_raw(mqtt, "grafana/disp/circ_active", uba_mon_wwm.sw2.circ_active ? "1" : "0");
        mqtt_publish(mqtt, "relay", "uba_ww_circ_daylight", uba_mon_wwm.sw2.circ_daylight);
        mqtt_publish(mqtt, "relay", "uba_ww_circ_manual", uba_mon_wwm.sw2.circ_manual);
        mqtt_publish(mqtt, "relay", "uba_ww_loading", uba_mon_wwm.sw2.is_loading);
      }
      if (CHECK_UPDATE(uba_mon_wwm, sw1, tel->h.offs, len)) {
        mqtt_publish(mqtt, "relay", "uba_ww_active", uba_mon_wwm.sw1.active);
        mqtt_publish(mqtt, "relay", "uba_ww_single_load", uba_mon_wwm.sw1.single_load);
        mqtt_publish(mqtt, "relay", "uba_ww_reloading", uba_mon_wwm.sw1.reloading);
        mqtt_publish(mqtt, "relay", "uba_ww_daylight", uba_mon_wwm.sw1.daylight_mode);
        mqtt_publish(mqtt, "relay", "uba_ww_desinfect", uba_mon_wwm.sw1.desinfect);
      }
      if (CHECK_UPDATE(uba_mon_wwm, fail, tel->h.offs, len)) {
        mqtt_publish(mqtt, "relay", "uba_ww_fail_desinfect", uba_mon_wwm.fail.desinfect);
        mqtt_publish(mqtt, "relay", "uba_ww_fail_probe1", uba_mon_wwm.fail.probe_1);
        mqtt_publish(mqtt, "relay", "uba_ww_fail_probe2", uba_mon_wwm.fail.probe_2);
        mqtt_publish(mqtt, "relay", "uba_ww_fail", uba_mon_wwm.fail.ww);
      }
    break;
    default: break;
  }
}

/* switch circ on boiler */
uint8_t msg_boiler_sw_circ [][7] = { { 0x8B, 0x08, 0x35, 0x00, 0x11, 0x01, 0x00 },    /* off */
                                     { 0x8B, 0x08, 0x35, 0x00, 0x11, 0x11, 0x00 } };  /* on  */
SIZE_TEST(msg_boiler_sw_circ, msg_boiler_sw_circ[0], 7);

/* switch circ on thermostat */
uint8_t msg_thermostat_sw_circ [][8] = { { 0x8B, 0x10, 0xFF, 0x03, 0x01, 0xF5, 0x00, 0x00 },    /* off */
                                         { 0x8B, 0x10, 0xFF, 0x03, 0x01, 0xF5, 0x01, 0x00 } };  /* on  */
SIZE_TEST(msg_thermostat_sw_circ, msg_thermostat_sw_circ[0], 8);


void ems_switch_circ(enum ems_device dev, int state) {
  LG_DEBUG("Switching circ on device 0x%02X to %d.", dev, state);
  state = state ? 1 : 0;
  switch (dev)
  {
    case EMS_DEV_BOILER:     mq_push(msg_boiler_sw_circ[state], sizeof(msg_boiler_sw_circ[0]), FALSE);         break;
    case EMS_DEV_THERMOSTAT: mq_push(msg_thermostat_sw_circ[state], sizeof(msg_thermostat_sw_circ[0]), FALSE); break;
    default:                 LG_ERROR("Switching circ on invalid device (0x%02X) to %d failed.", dev, state);     break;
  }
}


void ems_logic_evaluate_telegram(struct ems_telegram * tel, size_t len)
{
  if (logic)
  {
    len -=5;
    for(const struct ems_logic_cb * lcb = logic; lcb->cb != NULL; ++lcb)
    {
      int id = 0;
      for (const struct ems_logic_fct_val * val = lcb->test_val; val->len > 0; ++val)
      {
        if (val->tel_type == tel->h.type)
        {
          if (CHECK_NUM_UPDATE(val->offs, val->len, tel->h.offs, len))
            lcb->cb(id);
        }
        ++id;
      }
    }
  }
}

void print_telegram(int out, enum log_level loglevel, const char * prefix, uint8_t *msg, size_t len) {
    if (!log_get_level_state(loglevel))
        return;
    char text[3 + len * 3 + 2 + 1 + strlen(prefix) + 8];
    int pos = 0;
    pos += sprintf(&text[0], "%s (%02d) %cX:", prefix, len, out ? 'T' : 'R');

    for (size_t i = 0; i < len; i++) {
        pos += sprintf(&text[pos], " %02x", msg[i]);
        if (i == 3 || i == len - 2) {
            pos += sprintf(&text[pos], " ");
        }
    }
    log_push(loglevel, "%s", text);
}

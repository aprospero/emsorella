#include <arpa/inet.h>

#include "ems.h"
#include "tool/logger.h"
#include "ctrl/com/mqtt.h"

void ems_swap_telegram(struct ems_telegram * tel)
{
  switch (tel->h.type)
  {
    case ETT_UBA_MON_FAST:
    {
      struct ems_uba_monitor_fast * msg = &tel->d.uba_mon_fast;
      msg->err        = ntohs(msg->err);
      msg->fl_current = ntohs(msg->fl_current);
      msg->tmp.rl     = ntohs(msg->tmp.rl);
      msg->tmp.tmp1   = ntohs(msg->tmp.tmp1);
      msg->tmp.water  = ntohs(msg->tmp.water);
      msg->vl_ist     = ntohs(msg->vl_ist);
    }
    break;
    case ETT_UBA_MON_SLOW:
    {
      struct ems_uba_monitor_slow * msg = &tel->d.uba_mon_slow;
      msg->tmp_boiler  = ntohs(msg->tmp_boiler);
      msg->tmp_exhaust = ntohs(msg->tmp_exhaust);
      msg->tmp_out     = ntohs(msg->tmp_out);
    }
    break;
    case ETT_UBA_MON_WWM:
      tel->d.uba_mon_wwm.ist[0] = ntohs(tel->d.uba_mon_wwm.ist[0]);
      tel->d.uba_mon_wwm.ist[1] = ntohs(tel->d.uba_mon_wwm.ist[1]);
    break;
    default: break;
  }
}


void ems_log_telegram(enum log_level ll, struct ems_telegram * tel, size_t rx_len)
{
  float nan = 0.0 / 0.0;

  switch (tel->h.type)
  {
    case ETT_UBA_MON_FAST:
    {
      struct ems_uba_monitor_fast * msg = &tel->d.uba_mon_fast;

      log_push(ll, "Mon fast   VL: %04.1f°C/%04.1f°C (ist/soll)  RL: %04.1f °C     WW: %04.1f °C     Tmp?: %04.1f °C."
          , 0.1f * msg->vl_ist, 1.0f * msg->vl_soll, NANVAL(msg->tmp.rl), NANVAL(msg->tmp.water), NANVAL(msg->tmp.tmp1 ));
      log_push(ll, "  len: % 2u  Pump: %s       Blow: %s       Gas: %s      Ignite: %s      Circ: %s       Valve: %s.", rx_len
                ,ONOFF(msg->on.pump), ONOFF(msg->on.blower), ONOFF(msg->on.gas    ), ONOFF(msg->on.ignite ), ONOFF(msg->on.circ   ), ONOFF(msg->on.valve  ));
      log_push(ll, "  src: %02X  KsP: %03d%%/%03d%% (akt/max)    FlCurr: %04.1f µA Druck: %04.1f bar.", tel->h.src
                , msg->ks_akt_p, msg->ks_max_p, NANVAL(msg->fl_current), NANVA8(msg->sys_press));
      log_push(ll, "  dst: %02X  Error: %d  ServiceCode: %.2s.", tel->h.dst, msg->err, msg->service_code);
    }
    break;
    case ETT_UBA_MON_SLOW:
    {
      struct ems_uba_monitor_slow * msg = &tel->d.uba_mon_slow;

      log_push(ll, "Mon slow  Out: %04.1f °C    boiler: %04.1f °C    exhaust: %04.1f °C     Pump Mod: % 3u %%"
          , NANVAL(msg->tmp_out), NANVAL(msg->tmp_boiler), NANVAL(msg->tmp_exhaust), msg->pump_mod);
      log_push(ll, "  len: % 2u       Burner starts: % 8d       runtime: % 8d min    rt-stage2: % 8d min      rt-heating: % 8d min.", rx_len
                ,TRIVAL(msg->burner_starts), TRIVAL(msg->run_time), TRIVAL(msg->run_time_stage_2), TRIVAL(msg->run_time_heating));
      log_push(ll, "  src: %02X     dst: %02X.", tel->h.src, tel->h.dst);
    }
    break;
    case ETT_UBA_MON_WWM:
    {
      struct ems_uba_monitor_wwm * msg = &tel->d.uba_mon_wwm;

      log_push(ll, "Mon WWM   Ist: %04.1f°C/%04.1f°C  Soll: %04.1f °C     Lädt: %s      Durchfluss: %04.1f l/m."
          , NANVAL(msg->ist[0]), NANVAL(msg->ist[1]), 1.0f * msg->soll, ONOFF(msg->is_loading), NANVA8(msg->throughput));
      log_push(ll, "  len: % 2u Circ: %s       Man: %s       Day: %s      Single: %s      Day: %s       Reload: %s.", rx_len
                ,ONOFF(msg->circ_active), ONOFF(msg->circ_manual), ONOFF(msg->circ_daylight), ONOFF(msg->single_load ), ONOFF(msg->daylight_mode), ONOFF(msg->reloading));
      log_push(ll, "  src: %02X Type: %02X    prod: %s    Detox: %s     count: % 8u   time: % 8u min   temp: %s.", tel->h.src
                , msg->type, ONOFF(msg->active), ONOFF(msg->desinfect), TRIVAL(msg->op_count), TRIVAL(msg->op_time), msg->temp_ok ? " OK" : "NOK");
      log_push(ll, "  dst: %02X WWErr: %s    PR1Err: %s   PR2Err: %s   DesErr: %s.", tel->h.dst
               , ONOFF(msg->fail_ww), ONOFF(msg->fail_probe_1), ONOFF(msg->fail_probe_2), ONOFF(msg->fail_desinfect));
    }
    break;
    default: break;
  }

}

void ems_publish_telegram(struct mqtt_handle * mqtt, struct ems_telegram * tel)
{
  switch (tel->h.type)
  {
    case ETT_UBA_MON_FAST:
      mqtt_publish(mqtt, "sensor", "uba_water", tel->d.uba_mon_fast.tmp.water);
      mqtt_publish(mqtt, "sensor", "uba_vl_act", tel->d.uba_mon_fast.vl_ist);
      mqtt_publish(mqtt, "sensor", "uba_vl_nom", tel->d.uba_mon_fast.vl_soll);
      mqtt_publish(mqtt, "relay","uba_on_pump", tel->d.uba_mon_fast.on.pump);
      mqtt_publish(mqtt, "relay","uba_on_gas", tel->d.uba_mon_fast.on.gas);
      mqtt_publish(mqtt, "relay","uba_on_valve", tel->d.uba_mon_fast.on.valve);
      mqtt_publish(mqtt, "relay","uba_on_blower", tel->d.uba_mon_fast.on.blower);
      mqtt_publish(mqtt, "relay","uba_on_circ", tel->d.uba_mon_fast.on.circ);
      mqtt_publish(mqtt, "relay","uba_on_igniter", tel->d.uba_mon_fast.on.ignite);
      mqtt_publish(mqtt, "sensor","uba_power_act", tel->d.uba_mon_fast.ks_akt_p);
      mqtt_publish(mqtt, "sensor","uba_power_max", tel->d.uba_mon_fast.ks_max_p);
      mqtt_publish(mqtt, "sensor", "uba_flame_current", tel->d.uba_mon_fast.fl_current);
      mqtt_publish(mqtt, "sensor", "uba_error", tel->d.uba_mon_fast.err);
      mqtt_publish_formatted(mqtt, "sensor", "uba_service_code", "%.2s", tel->d.uba_mon_fast.service_code, tel->d.uba_mon_fast.service_code);
    break;
    case ETT_UBA_MON_SLOW:
      mqtt_publish(mqtt, "sensor", "uba_outside", tel->d.uba_mon_slow.tmp_out);
      mqtt_publish(mqtt, "sensor", "uba_rt", TRIVAL(tel->d.uba_mon_slow.run_time));
      mqtt_publish(mqtt, "sensor", "uba_pump_mod", tel->d.uba_mon_slow.pump_mod);
 //     mqtt_publish(mqtt, "sensor", "uba_rt_heating", TRIVAL(tel->d.uba_mon_slow.run_time_heating));
 //     mqtt_publish(mqtt, "sensor", "uba_rt_stage2", TRIVAL(tel->d.uba_mon_slow.run_time_stage_2));
 //     mqtt_publish(mqtt, "sensor", "uba_burner_starts", TRIVAL(tel->d.uba_mon_slow.burner_starts));
    break;
    case ETT_UBA_MON_WWM:
      mqtt_publish(mqtt, "sensor", "uba_ww_act1", tel->d.uba_mon_wwm.ist[0]);
      mqtt_publish(mqtt, "sensor", "uba_ww_nom", tel->d.uba_mon_wwm.soll);
      mqtt_publish(mqtt, "relay", "uba_ww_circ_active", tel->d.uba_mon_wwm.circ_active);
      mqtt_publish(mqtt, "relay", "uba_ww_circ_daylight", tel->d.uba_mon_wwm.circ_daylight);
      mqtt_publish(mqtt, "relay", "uba_ww_circ_manual", tel->d.uba_mon_wwm.circ_manual);
      mqtt_publish(mqtt, "relay", "uba_ww_loading", tel->d.uba_mon_wwm.is_loading);
      mqtt_publish(mqtt, "relay", "uba_ww_active", tel->d.uba_mon_wwm.active);
      mqtt_publish(mqtt, "relay", "uba_ww_single_load", tel->d.uba_mon_wwm.single_load);
      mqtt_publish(mqtt, "relay", "uba_ww_reloading", tel->d.uba_mon_wwm.reloading);
      mqtt_publish(mqtt, "relay", "uba_ww_daylight", tel->d.uba_mon_wwm.daylight_mode);
      mqtt_publish(mqtt, "relay", "uba_ww_desinfect", tel->d.uba_mon_wwm.desinfect);
      mqtt_publish(mqtt, "relay", "uba_ww_fail_desinfect", tel->d.uba_mon_wwm.fail_desinfect);
      mqtt_publish(mqtt, "relay", "uba_ww_fail_probe1", tel->d.uba_mon_wwm.fail_probe_1);
      mqtt_publish(mqtt, "relay", "uba_ww_fail_probe2", tel->d.uba_mon_wwm.fail_probe_2);
      mqtt_publish(mqtt, "relay", "uba_ww_fail", tel->d.uba_mon_wwm.fail_ww);
    break;
    default: break;
  }
}


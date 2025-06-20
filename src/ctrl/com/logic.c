#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <time.h>

#include "ctrl/com/ems.h"
#include "ctrl/logger.h"

#define sizeof_member(TYPE, MEMBER) (sizeof( ((TYPE *)0)->MEMBER ))

//extern struct ems_plus_t01a5       emsplus_t01a5;
extern struct ems_uba_monitor_fast uba_mon_fast;
//extern struct ems_uba_monitor_slow uba_mon_slow;
extern struct ems_uba_monitor_wwm  uba_mon_wwm;

struct logic_circ_state
{
  time_t   last_on;     /* timestamp of last active circulation */
  time_t   last_start;  /* timestamp of last active circ start */
  uint16_t threshold;   /* variable threshold at which circ is activated */
  uint8_t  is_valid;    /* bitwise valid mask for input values */
  uint8_t  started;     /* flag for having sent a circ activation message */
};

struct logic_state
{
  struct logic_circ_state circ;
} state;

void logic_circulation(int val_id);

struct ems_logic_fct_val circ_vals[] =
{
  { ETT_UBA_MON_FAST, offsetof(struct ems_uba_monitor_fast, tmp.water), sizeof_member(struct ems_uba_monitor_fast, tmp.water) },
  { ETT_UBA_MON_WWM, offsetof(struct ems_uba_monitor_wwm, sw2), sizeof_member(struct ems_uba_monitor_wwm, sw2) },
  { 0, 0, 0 }
};

struct ems_logic_cb logics[] =
{{ logic_circulation, circ_vals },
 { NULL, NULL}
};


void logic_init(const struct ems_logic_cb ** logic_cb)
{
  memset(&state, 0, sizeof(state));
  if (logic_cb != NULL)
    *logic_cb = logics;
}

#define LGK_CIRC_NOM_TEMP      650 /* temperature where auto circulation is activated (centigrade celsius) */
#define LGK_CIRC_HYSTERESIS      5 /* difference between auto circulation on and off state (centigrade) */
#define LGK_CIRC_INTERVAL     1200 /* auto circulation is applied in intervals of <x> seconds */
#define LGK_CIRC_DURATION      180 /* active circulation per interval for <x> seconds */
#define LGK_CIRC_MAX_DURATION 1200 /* any circulation has a max duration before auto off */

void logic_circulation(int val_id)
{
  if (state.circ.is_valid != 0x03)
  {
    state.circ.is_valid |= 1 << val_id;
    if (state.circ.is_valid != 0x03)
      return;
    state.circ.threshold  = LGK_CIRC_NOM_TEMP;
    state.circ.last_start = 0;
    state.circ.started = FALSE;
    LG_INFO("Water temp Logic initilized. Threshold: %5.1f°C, Hysteresis: %5.1f°C. Interval: %ds, Duration: %ds, Auto-Off after %ds."
             , 0.1 * LGK_CIRC_NOM_TEMP, 0.1 * LGK_CIRC_HYSTERESIS, LGK_CIRC_INTERVAL, LGK_CIRC_DURATION, LGK_CIRC_MAX_DURATION);
  }

  int    temp    = uba_mon_fast.tmp.water;
  int    circ_on = uba_mon_wwm.sw2.circ_active;
  time_t now     = time(NULL);

//  if (now == 0) /* never ever let now be zero. */
//    now = 1;

  /* capture actual state */
  if (circ_on)
  {
    state.circ.last_on = now;
    if (state.circ.last_start == 0)
      state.circ.last_start = now;
  }
  else
  {
    state.circ.last_start = 0;
  }

  /* make decisions */
  if (temp >= state.circ.threshold) /* switch on intervall circulation */
  {
    if (now - state.circ.last_on > LGK_CIRC_INTERVAL - LGK_CIRC_DURATION) /* interval start */
    {
      LG_INFO("Water temp %5.1f°C (>=%5.1f°C). Activate auto circulation.", 0.1 * temp, 0.1 * state.circ.threshold);
      ems_switch_circ(EMS_DEV_THERMOSTAT, TRUE);
      state.circ.last_on = now;
      if (state.circ.last_start == 0)
        state.circ.last_start = now;
      state.circ.threshold = LGK_CIRC_NOM_TEMP - LGK_CIRC_HYSTERESIS;
      state.circ.started = TRUE;
    }
    else if (state.circ.started && state.circ.last_start != 0 && now - state.circ.last_start > LGK_CIRC_DURATION) /* interval stop */
    {
      LG_INFO("Water temp %5.1f°C (>=%5.1f°C). Interval deactivate auto Circulation.", 0.1 * temp, 0.1 * state.circ.threshold);
      ems_switch_circ(EMS_DEV_THERMOSTAT, FALSE);
      state.circ.started = FALSE;
    }
  }
  else
  {
    if (state.circ.started == TRUE)
    {
      LG_INFO("Water temp %5.1f°C (<%5.1f°C). Deactivate auto Circulation.", 0.1 * temp, 0.1 * state.circ.threshold);
      if (state.circ.last_on == now)
      {
        ems_switch_circ(EMS_DEV_THERMOSTAT, FALSE);
      }
      state.circ.threshold = LGK_CIRC_NOM_TEMP;
      state.circ.started = FALSE;
    }
  }
  if (state.circ.last_start != 0 && now - state.circ.last_start >= LGK_CIRC_MAX_DURATION)
  {
    LG_INFO("Circulation pump is running for over %4.1f min. Auto-off.", 1.0f * LGK_CIRC_MAX_DURATION / 60.0f);
    ems_switch_circ(EMS_DEV_THERMOSTAT, FALSE);
  }
}

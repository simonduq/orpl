#include "contiki.h"
#include "node-id.h"
#include "simple-energest.h"
#include "rpl-tools.h"

static uint32_t last_tx, last_rx, last_time;
static uint32_t delta_tx, delta_rx, delta_time;
static uint32_t curr_tx, curr_rx, curr_time;

PROCESS(simple_energest_process, "Simple Energest");
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(simple_energest_process, ev, data)
{
  static struct etimer periodic;
  PROCESS_BEGIN();
  etimer_set(&periodic, 60 * CLOCK_SECOND);

  while(1) {
    PROCESS_WAIT_UNTIL(etimer_expired(&periodic));
    etimer_reset(&periodic);
    simple_energest_step();
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/
void simple_energest_start() {
  energest_flush();
  last_tx = energest_type_time(ENERGEST_TYPE_TRANSMIT);
  last_rx = energest_type_time(ENERGEST_TYPE_LISTEN);
  last_time = energest_type_time(ENERGEST_TYPE_CPU) + energest_type_time(ENERGEST_TYPE_LPM);
  process_start(&simple_energest_process, NULL);
}

/*---------------------------------------------------------------------------*/
void simple_energest_step() {
  static uint16_t cpt;
  energest_flush();

  curr_tx = energest_type_time(ENERGEST_TYPE_TRANSMIT);
  curr_rx = energest_type_time(ENERGEST_TYPE_LISTEN);
  curr_time = energest_type_time(ENERGEST_TYPE_CPU) + energest_type_time(ENERGEST_TYPE_LPM);

  delta_tx = curr_tx - last_tx;
  delta_rx = curr_rx - last_rx;
  delta_time = curr_time - last_time;

  last_tx = curr_tx;
  last_rx = curr_rx;
  last_time = curr_time;

  uint32_t fraction = (100ul*(delta_tx+delta_rx))/delta_time;
  rpl_trace_null("Duty Cycle: [%u %u] %8lu +%8lu /%8lu (%lu %%)",
      node_id,
      cpt++,
      delta_tx, delta_rx, delta_time,
      fraction
  );

  if(cpt % 8 == 0) {
    debug_ranks();
  }
}

#ifndef RPL_TOOLS_H
#define RPL_TOOLS_H

#include "net/uip.h"

extern uip_ipaddr_t prefix;

#define EDC_DIVISOR 128
#define EDC_TICKS_TO_METRIC(edc) (uint16_t)((edc) / (CONTIKIMAC_CONF_CYCLE_TIME / EDC_DIVISOR))

void create_rpl_dag(uip_ipaddr_t *ipaddr);
uip_ipaddr_t * set_global_address(void);
void set_addr_iid(uip_ipaddr_t *ipaddr, uint16_t id);
void node_ip6addr(uip_ipaddr_t *ipaddr, uint16_t id);
void rpl_setup(int is_root, uint16_t id);

#define APP_PAYLOAD_LEN 64
struct app_data {
  uint32_t seqno;
  uint16_t src;
  uint16_t dest;
  uint8_t hop;
  uint8_t ping;
  uint8_t fpcount;
};

void rpl_trace(struct app_data *data);
struct app_data *rpl_dataptr_from_uip();
struct app_data *rpl_dataptr_from_packetbuf();
void app_data_init(struct app_data *dst, struct app_data *src);
void update_e2e_edc(int verbose);
void debug_ranks();
int time_elapsed();

extern int forwarder_set_size;
extern int neighbor_set_size;
extern int curr_dio_interval;
extern uint16_t rank;
extern rtimer_clock_t start_time;

#endif


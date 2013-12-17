#ifndef RPL_TOOLS_H
#define RPL_TOOLS_H

#include "net/uip.h"

void create_rpl_dag(uip_ipaddr_t *ipaddr);
uip_ipaddr_t * set_global_address(void);
void set_addr_iid(uip_ipaddr_t *ipaddr, uint16_t id);
void node_ip6addr(uip_ipaddr_t *ipaddr, uint16_t id);
uip_ipaddr_t *tools_setup_addresses(uint16_t id);

#define APP_PAYLOAD_LEN 64
struct app_data {
  uint32_t seqno;
  uint16_t src;
  uint16_t dest;
  uint8_t hop;
  uint8_t ping;
  uint8_t fpcount;
};

void rpl_log(struct app_data *data);
struct app_data *rpl_dataptr_from_uip();
struct app_data *rpl_dataptr_from_packetbuf();
void app_data_init(struct app_data *dst, struct app_data *src);
void update_e2e_edc(int verbose);
void debug_ranks();
int time_elapsed();
void orpl_set_addr_iid_from_id(uip_ipaddr_t *ipaddr, uint16_t id);

extern int forwarder_set_size;
extern int neighbor_set_size;
extern int curr_dio_interval;
extern uint16_t rank;

#define rpl_trace(...) printf(__VA_ARGS__)
#define rpl_trace_from_dataptr(appdataptr, ...) { printf(__VA_ARGS__); rpl_log(appdataptr); }
#define rpl_trace_null(...) rpl_trace_from_dataptr(NULL, __VA_ARGS__)
#define rpl_trace_from_uip(...) rpl_trace_from_dataptr(rpl_dataptr_from_uip(), __VA_ARGS__)
#define rpl_trace_from_packetbuf(...) rpl_trace_from_dataptr(rpl_dataptr_from_packetbuf(), __VA_ARGS__)

#endif

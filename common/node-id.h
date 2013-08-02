#include "deployment.h"
#include "sys/node-id.h"
#include "net/uip.h"
#include "contiki-conf.h"
#include "dev/xmem.h"
#include "net/rime/rimeaddr.h"
#include "net/uip-ds6.h"

extern unsigned short node_id;

uint16_t node_id_from_rimeaddr(const rimeaddr_t *addr);
uint16_t node_id_from_lipaddr(const uip_ipaddr_t *addr);
uint16_t node_id_from_ipaddr(const uip_ipaddr_t *addr);
void node_id_restore();
uint16_t get_id(uint16_t index);
uint16_t get_n_nodes();
uint16_t get_node_id(uint16_t index);
uint16_t get_random_id();
int is_addressable();
int is_id_addressable(uint16_t id);
int is_id_in_any_to_any(uint16_t id);
int is_in_any_to_any();
int id_has_outage(uint16_t id);
int has_outage(void);

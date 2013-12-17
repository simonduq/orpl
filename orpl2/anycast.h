#ifndef ANYCAST_H
#define ANYCAST_H

#include "net/rpl/rpl.h"
#include "net/netstack.h"
#include "net/mac/frame802154.h"
#include "net/uip-ds6.h"
#include "rpl-tools.h"
#include "bloom.h"

#define EDC_DIVISOR 128
#define EDC_TICKS_TO_METRIC(edc) (uint16_t)((edc) / (CONTIKIMAC_CONF_CYCLE_TIME / EDC_DIVISOR))

extern double_bf dbf;

#define EXTRA_ACK_LEN 10

#define DO_ACK            1 /* Set if a link-layer ack must be sent */
#define DO_FORWARD        2 /* Set if the link-layer ack must contain the "forward" bit */
#define IS_ANYCAST        4 /* Set if the packet is a anycast */
#define FROM_SUBDODAG     8 /* Set if the packet is coming from the sub-dodag (going upwards) */
#define IS_RECOVERY       16 /* Set if the packet comes from false positive recovery */

extern rimeaddr_t anycast_addr_up;
extern rimeaddr_t anycast_addr_down;
extern rimeaddr_t anycast_addr_nbr;
extern rimeaddr_t anycast_addr_recover;
extern uint16_t hbh_edc;
extern uint16_t e2e_edc;
extern uint32_t anycast_count_incomming;
extern uint32_t anycast_count_acked;
extern uint32_t bloom_merged_count;
extern int sending_bloom;
extern int is_edc_root;

uint8_t frame80254_parse_anycast_irq(uint8_t *data, uint8_t len);
uint8_t frame80254_parse_anycast_process(uint8_t *data, uint8_t len, int acked, uint16_t *rank);
void anycast_packet_sent();
void anycast_packet_received();
void update_e2e_edc();
int is_in_subdodag(uip_ipaddr_t *ipv6);
int is_reachable_neighbor(uip_ipaddr_t *ipv6);
void anycast_set_packetbuf_addr();
void orpl_trickle_callback(rpl_instance_t *instance);
void broadcast_acked(const rimeaddr_t *receiver);
void broadcast_done();
void bloom_broacast_failed();
void blacklist_insert(uint32_t seqno);
int blacklist_contains(uint32_t seqno);
void acked_down_insert(uint32_t seqno, uint16_t id);
int acked_down_contains(uint32_t seqno, uint16_t id);
void received_noip();
void anycast_init(int is_sink, int up_only);
void softack_acked_callback(const uint8_t *buf, uint8_t len);
void softack_input_callback(const uint8_t *buf, uint8_t len, uint8_t **ackbufptr, uint8_t *acklen);

#endif

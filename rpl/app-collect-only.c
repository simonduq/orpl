#include "deployment.h"
#include "contiki.h"
#include "lib/random.h"
#include "sys/ctimer.h"
#include "sys/etimer.h"
#include "net/uip.h"
#include "net/uip-ds6.h"
#include "net/uip-debug.h"
#include "net/packetbuf.h"
#include "net/rpl/rpl.h"
#include "net/netstack.h"

#include "node-id.h"
#include "simple-energest.h"

#include "simple-udp.h"
#include "rpl-tools.h"

#include <stdio.h>
#include <string.h>

#if (UP_ONLY == 0)
#error UP_ONLY is not set
#endif

#define SEND_INTERVAL   (4 * 60 * CLOCK_SECOND)
#define UDP_PORT 1234

static char buf[APP_PAYLOAD_LEN];
static struct simple_udp_connection unicast_connection;

/*---------------------------------------------------------------------------*/
PROCESS(unicast_sender_process, "ORPL -- Collect-only Application");
AUTOSTART_PROCESSES(&unicast_sender_process);
/*---------------------------------------------------------------------------*/
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *data,
         uint16_t datalen)
{
  printf("App: received");
  rpl_trace((struct app_data*)data);
}
/*---------------------------------------------------------------------------*/
void app_send_to(uint16_t id) {
  static unsigned int cpt;
  struct app_data data;
  uip_ipaddr_t dest_ipaddr;

  data.seqno = ((uint32_t)node_id << 16) + cpt;
  data.src = node_id;
  data.dest = id;
  data.hop = 0;
  data.fpcount = 0;

  node_ip6addr(&dest_ipaddr, id);

  printf("App: sending");
  rpl_trace(&data);

  *((struct app_data*)buf) = data;
  simple_udp_sendto(&unicast_connection, buf, sizeof(buf) + 1, &dest_ipaddr);

  cpt++;
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_sender_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer send_timer;

  PROCESS_BEGIN();

  random_rand();
  simple_energest_start();

  if(node_id == 0) {
    NETSTACK_RDC.off(0);
    uint16_t mymac = rimeaddr_node_addr.u8[7] << 8 | rimeaddr_node_addr.u8[6];
    printf("Node id unset, my mac is 0x%04x\n", mymac);
    PROCESS_EXIT();
  }

  //  etimer_set(&periodic_timer, 90 * CLOCK_SECOND);
  //  PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
  printf("App: %u starting\n", node_id);

  rpl_setup(node_id == ROOT_ID, node_id);
  simple_udp_register(&unicast_connection, UDP_PORT,
                      NULL, UDP_PORT, receiver);

  if(node_id == ROOT_ID) {
    NETSTACK_RDC.off(1);
  } else {
    etimer_set(&periodic_timer, 2 * 60 * CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    etimer_set(&periodic_timer, SEND_INTERVAL);
    while(1) {
      etimer_set(&send_timer, random_rand() % (SEND_INTERVAL));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));

      if(rank != 0xffff) {
        app_send_to(ROOT_ID);
      } else {
        printf("App: not in DODAG (%u %u)\n", node_id, ROOT_ID);
      }

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      etimer_reset(&periodic_timer);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

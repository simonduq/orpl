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

#if (UP_ONLY != 0)
#error UP_ONLY is set
#endif

#if (ALL_NODES_ADDRESSABLE != 0)
#error ALL_NODES_ADDRESSABLE is set
#endif

#define SEND_INTERVAL     (4 * CLOCK_SECOND)
#define UDP_PORT 1234

static char buf[APP_PAYLOAD_LEN];
static struct simple_udp_connection unicast_connection;

#define TARGET_REACHABLE_RATIO 40
//#define TARGET_REACHABLE_RATIO 50
//#define TARGET_REACHABLE_RATIO 80
//#define TARGET_REACHABLE_RATIO 100

/*---------------------------------------------------------------------------*/
PROCESS(unicast_sender_process, "ORPL -- Up and Down Application");
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
int check_reachable_count() {
  int i;
  int count = 0;

  for(i=0; i<get_n_nodes(); i++) {
    uip_ipaddr_t dest_ipaddr;
    uint8_t id = get_node_id(i);
    if(id == ROOT_ID) {
      continue;
    }
    node_ip6addr(&dest_ipaddr, id);
    if(uip_ds6_route_lookup(&dest_ipaddr)) {
      count++;
    }
  }

  printf("App: nodes reachable: %u/%u\n", count, get_n_nodes()-1 /* exclude sink */);
  return count >= ((get_n_nodes()-1) * (TARGET_REACHABLE_RATIO)) / 100;
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_sender_process, ev, data)
{
  static struct etimer periodic_timer;

  PROCESS_BEGIN();

  if(node_id == 0) {
    NETSTACK_RDC.off(0);
    uint16_t mymac = rimeaddr_node_addr.u8[7] << 8 | rimeaddr_node_addr.u8[6];
    printf("Node id unset, my mac is 0x%04x\n", mymac);
    PROCESS_EXIT();
  }

  random_rand();
  simple_energest_start();

  rpl_setup(node_id == ROOT_ID, node_id);
  simple_udp_register(&unicast_connection, UDP_PORT,
                      NULL, UDP_PORT, receiver);

  if(node_id == ROOT_ID) {
    NETSTACK_RDC.off(1);
//    etimer_set(&periodic_timer, 1 * 60 * CLOCK_SECOND);
//    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    printf("App: %u starting\n", node_id);
    etimer_set(&periodic_timer, SEND_INTERVAL);
    while(1) {
      if(check_reachable_count()) {
        uip_ipaddr_t dest_ipaddr;
//        static int index;
        int id;
        do {
//          id = get_node_id(index++);
          id = get_random_id();
          node_ip6addr(&dest_ipaddr, id);
        } while (id == ROOT_ID || !uip_ds6_route_lookup(&dest_ipaddr));
        app_send_to(id);
      }

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      etimer_reset(&periodic_timer);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

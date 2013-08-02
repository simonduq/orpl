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

#define TIME_BEFORE_APP 12
#define SEND_INTERVAL     (2 * 60 * CLOCK_SECOND)
#define UDP_PORT 1234

static char buf[APP_PAYLOAD_LEN];
static struct simple_udp_connection unicast_connection;
static uint16_t current_cpt = 0;

/*---------------------------------------------------------------------------*/
PROCESS(unicast_sender_process, "ORPL -- Up and Down Application");
AUTOSTART_PROCESSES(&unicast_sender_process);
void app_send_to(uint16_t id, int ping, uint32_t seqno);
/*---------------------------------------------------------------------------*/
static void
receiver(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *dataptr,
         uint16_t datalen)
{
  struct app_data data;
  app_data_init(&data, (struct app_data*)dataptr);
  if(data.ping) {
    printf("App: received ping");
  } else {
    printf("App: received pong");
  }
  rpl_trace((struct app_data*)dataptr);
  if(data.ping) {
    app_send_to(data.src, 0, data.seqno | 0x8000l);
  }
}
//*---------------------------------------------------------------------------*/
void app_send_to(uint16_t id, int ping, uint32_t seqno) {
  struct app_data data;
  uip_ipaddr_t dest_ipaddr;

  data.seqno = seqno;
  data.src = node_id;
  data.dest = id;
  data.hop = 0;
  data.ping = ping;
  data.fpcount = 0;

  node_ip6addr(&dest_ipaddr, id);

  if(ping) {
    printf("App: sending ping");
  } else {
    printf("App: sending pong");
  }
  rpl_trace(&data);

  *((struct app_data*)buf) = data;
  simple_udp_sendto(&unicast_connection, buf, sizeof(buf) + 1, &dest_ipaddr);
}

/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_sender_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer send_timer;

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
  } else if(is_in_any_to_any()) {
    etimer_set(&periodic_timer, TIME_BEFORE_APP * 60 * CLOCK_SECOND);
    PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
    printf("App: %u starting\n", node_id);
    etimer_set(&periodic_timer, SEND_INTERVAL);

    static uint16_t index;
    index = random_rand();

    while(1) {
      etimer_set(&send_timer, random_rand() % (SEND_INTERVAL));
      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&send_timer));

      uip_ipaddr_t dest_ipaddr;
      uint16_t id;

      do {
        id = get_node_id(index++);
        node_ip6addr(&dest_ipaddr, id);
      } while (id == node_id || !is_id_in_any_to_any(id));

      if(id < node_id || id == ROOT_ID) {
        /* After finding an addressable node, send only if destination has lower ID
         * otherwise, next attempt will be at the next period */
        app_send_to(id, 1, ((uint32_t)node_id << 16) + current_cpt);
        current_cpt++;
      }

      PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
      etimer_reset(&periodic_timer);
    }
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

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
#if WITH_ORPL
#include "orpl.h"
#endif /* WITH_ORPL */

#include "node-id.h"

#include "simple-udp.h"

#include <stdio.h>
#include <string.h>

#define SEND_INTERVAL   (15 * CLOCK_SECOND)
#define UDP_PORT 1234
#define ROOT_ID 1
#define SRC_ID 6
#define DEST_ID 8

static unsigned int last_cnt;
static clock_time_t last_sent_time;
static uip_ipaddr_t global_ipaddr;
static struct simple_udp_connection unicast_connection;

/*---------------------------------------------------------------------------*/
PROCESS(unicast_sender_process, "CALIPSO -- example use case");
AUTOSTART_PROCESSES(&unicast_sender_process);
/*---------------------------------------------------------------------------*/
void app_send_to(uint16_t id, int ping, int cnt) {
  uip_ipaddr_t dest_ipaddr;

  printf("App: sending %s %u\n", ping ? "ping" : "pong", cnt);

  uip_ip6addr(&dest_ipaddr, 0xaaaa, 0, 0, 0, 0x0212, 0x7400 | id, id, (id << 8) | id);
  simple_udp_sendto(&unicast_connection, &cnt, sizeof(cnt), &dest_ipaddr);
}
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
  unsigned int cnt = *((unsigned int*)data);
  if(node_id == DEST_ID) {
    printf("App: received ping %u\n", cnt);
	app_send_to(SRC_ID, 0, cnt);
  } else if(node_id == SRC_ID) {
	if(cnt == last_cnt) {
	  clock_time_t delay = clock_time() - last_sent_time;
	  printf("App: received pong %u with delay %lu ms\n", cnt, (1000*delay)/CLOCK_SECOND);
	} else {
	  printf("App: received old pong %u\n", cnt);
	}
  }
}
/*---------------------------------------------------------------------------*/
void
create_rpl_dag(uip_ipaddr_t *ipaddr)
{
  struct uip_ds6_addr *root_if;

  root_if = uip_ds6_addr_lookup(ipaddr);
  if(root_if != NULL) {
    rpl_dag_t *dag;

    rpl_set_root(RPL_DEFAULT_INSTANCE, ipaddr);
    dag = rpl_get_any_dag();
    rpl_set_prefix(dag, &global_ipaddr, 64);
  }
}
/*---------------------------------------------------------------------------*/
PROCESS_THREAD(unicast_sender_process, ev, data)
{
  static struct etimer periodic_timer;
  static struct etimer send_timer;

  PROCESS_BEGIN();

  printf("App: %u starting\n", node_id);

  uip_ip6addr(&global_ipaddr, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  uip_ds6_set_addr_iid(&global_ipaddr, &uip_lladdr);
  uip_ds6_addr_add(&global_ipaddr, 0, ADDR_AUTOCONF);

  if(node_id == ROOT_ID) {
	rpl_dag_t *dag = rpl_set_root(RPL_DEFAULT_INSTANCE, &global_ipaddr);
	rpl_set_prefix(dag, &global_ipaddr, 64);
	create_rpl_dag(&global_ipaddr);
	NETSTACK_RDC.off(1);
  }

#if WITH_ORPL
  orpl_init(node_id == ROOT_ID, 0);
#endif /* WITH_ORPL */

  simple_udp_register(&unicast_connection, UDP_PORT,
					  NULL, UDP_PORT, receiver);

  if(node_id == SRC_ID) {
	etimer_set(&periodic_timer, 8 * 60 * CLOCK_SECOND);
	PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&periodic_timer));
	etimer_set(&periodic_timer, SEND_INTERVAL);
	while(1) {
	  static unsigned int cnt;
	  etimer_set(&send_timer, random_rand() % (SEND_INTERVAL));
	  PROCESS_WAIT_UNTIL(etimer_expired(&send_timer));

	  last_cnt = cnt;
	  last_sent_time = clock_time();
	  app_send_to(DEST_ID, 1, cnt++);

	  PROCESS_WAIT_UNTIL(etimer_expired(&periodic_timer));
	  etimer_reset(&periodic_timer);
	}
  }

  PROCESS_END();
}
/*---------------------------------------------------------------------------*/

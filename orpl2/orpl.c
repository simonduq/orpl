#include "deployment.h"
#include "cooja-debug.h"
#include "orpl.h"
#include "orpl-anycast.h"
#include "net/packetbuf.h"
#include "net/rpl/rpl.h"
#include "net/nbr-table.h"
#if IN_COOJA
#define DEBUG DEBUG_ANNOTATE
//#define DEBUG DEBUG_NONE
#else
#define DEBUG DEBUG_NONE
#endif
#include "net/uip-debug.h"
#include "net/uip-ds6.h"
#include "routing-set.h"
#include "node-id.h"
#include "orpl-log.h"
#include "random.h"
#include "net/rpl/rpl-private.h"
#include <string.h>

#if IN_COOJA
#define TEST_FALSE_POSITIVES 1
#else
#define TEST_FALSE_POSITIVES 0
#endif

#define ALL_NEIGHBORS_IN_FILTER 0

#if (CMD_CYCLE_TIME >= 250)
#define NEIGHBOR_PRR_THRESHOLD 50
#else
#define NEIGHBOR_PRR_THRESHOLD 35
#endif

/* For tests. When set:
 * - stop updating EDC after N minutes
 * - start updating Bloom sets only after N+1 minutes
 * - don't age Bloom sets */
#ifndef FREEZE_TOPOLOGY
#define FREEZE_TOPOLOGY 1
#endif

#if FREEZE_TOPOLOGY
#define UPDATE_EDC_MAX_TIME 2
#define UPDATE_BLOOM_MIN_TIME 3
#else
#define UPDATE_EDC_MAX_TIME 0
#define UPDATE_BLOOM_MIN_TIME 0
#endif

#define ACKED_DOWN_SIZE 32

#define UIP_IP_BUF ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])

#define BLOOM_MAGIC 0x83d9
#define RANK_MAX_CHANGE (2*EDC_DIVISOR)

uint32_t orpl_broadcast_count = 0;

void rpl_link_neighbor_callback(const rimeaddr_t *addr, int status, int numtx);


static rtimer_clock_t start_time;

int
orpl_is_root()
{
  return orpl_current_edc() == 0;
}

rpl_rank_t
orpl_current_edc()
{
  rpl_dag_t *dag = rpl_get_any_dag();
  return dag == NULL ? 0xffff : dag->rank;
}

int time_elapsed() {
  return (RTIMER_NOW()-start_time)/(RTIMER_ARCH_SECOND*60);
}

static int orpl_up_only = 0;

int
orpl_is_topology_frozen()
{
  if(FREEZE_TOPOLOGY && orpl_up_only == 0) {
    if(time_elapsed() > UPDATE_EDC_MAX_TIME) {
      return 1;
    }
  }
  return 0;
}

struct bloom_broadcast_s {
  uint16_t magic; /* we need a magic number here as this goes straight on top of 15.4 mac
   * and we need to way to check whether incoming data is a bloom broadcast or not */
  uint16_t rank;
  union {
    routing_set filter;
    uint8_t padding[64];
  };
};

struct acked_down {
  uint32_t seqno;
  uint16_t id;
};

uint32_t routing_set_merged_count = 0;
uint32_t anycast_count_incomming;
uint32_t anycast_count_acked;

int sending_bloom = 0;
int is_edc_root = 0;

static struct bloom_broadcast_s bloom_broadcast;
static struct acked_down acked_down[ACKED_DOWN_SIZE];
uint16_t last_broadcasted_rank = 0xffff;
rpl_dag_t *curr_dag;
rpl_instance_t *curr_instance;
static struct ctimer broadcast_bloom_timer;
//static int bit_count_last = 0;

void check_neighbors();
int test_prr(uint16_t count, uint16_t threshold);
void bloom_received(struct bloom_broadcast_s *data);
void bloom_request_broadcast();

int is_node_addressable(uip_ipaddr_t *ipv6) {
  return 1;
}

/* Bloom filter false positive black list */
#define BLACKLIST_SIZE 16
static uint32_t blacklisted_seqnos[BLACKLIST_SIZE];

void blacklist_insert(uint32_t seqno) {
  printf("Bloom: blacklisting %lx\n", seqno);
  int i;
  for(i = BLACKLIST_SIZE - 1; i > 0; --i) {
    blacklisted_seqnos[i] = blacklisted_seqnos[i - 1];
  }
  blacklisted_seqnos[0] = seqno;
}

int blacklist_contains(uint32_t seqno) {
  int i;
  for(i = 0; i < BLACKLIST_SIZE; ++i) {
    if(seqno == blacklisted_seqnos[i]) {
      return 1;
    }
  }
  return 0;
}

void acked_down_insert(uint32_t seqno, uint16_t id) {
  printf("Bloom: inserted ack down %lx %u\n", seqno, id);
  int i;
  for(i = ACKED_DOWN_SIZE - 1; i > 0; --i) {
    acked_down[i] = acked_down[i - 1];
  }
  acked_down[0].seqno = seqno;
  acked_down[0].id = id;
}

int acked_down_contains(uint32_t seqno, uint16_t id) {
  int i;
  for(i = 0; i < ACKED_DOWN_SIZE; ++i) {
    if(seqno == acked_down[i].seqno && id == acked_down[i].id) {
      return 1;
    }
  }
  return 0;
}

void orpl_print_ranks() {
  rpl_rank_t curr_edc = orpl_current_edc();
  rpl_parent_t *p;
  printf("Ackcount: start\n");
  for(p = nbr_table_head(rpl_parents);
        p != NULL;
        p = nbr_table_next(rpl_parents, p)) {
    uint16_t count = p->bc_ackcount;
    uint16_t neighbor_rank = p->rank;
    rimeaddr_t *addr = nbr_table_get_lladdr(rpl_parents, p);
    uint16_t neighbor_id = node_id_from_rimeaddr(addr);
    if(neighbor_id == 0) {
      printf("Ackcount: [0] -> ");
      uip_debug_lladdr_print((const uip_lladdr_t *)addr);
      printf("\n");
    } else {
      printf("Ackcount: [%u] %u/%lu (%u %u -> %u) ->", neighbor_id, count, orpl_broadcast_count, curr_edc, neighbor_rank,
          (neighbor_rank != 0xffff && neighbor_rank > curr_edc && test_prr(count, NEIGHBOR_PRR_THRESHOLD))?1:0);
      uip_debug_lladdr_print((const uip_lladdr_t *)addr);
            printf("\n");
    }
  }
  printf("Ackcount: end\n");

  routing_set_print();

  uint16_t i;
  int count = 0;
  int print_header = 1;
  printf("BFlist: start\n");
  for(i=0; i<get_n_nodes(); i++) {
    if(print_header) {
      printf("BFlist: [%2u]", count/8);
      print_header = 0;
    }
    uip_ipaddr_t dest_ipaddr;
    uint16_t id = get_node_id_from_index(i);
    set_ipaddr_from_id(&dest_ipaddr, id);
    int contained = is_in_subdodag(&dest_ipaddr);
    if(contained) {
      count+=1;
      printf("%3u, ", id);
      if(count%8 == 0) {
        printf("\n");
        print_header = 1;
      }
    }
  }
  printf("\nBFlist: end (%u nodes)\n",count);
}

int test_prr(uint16_t count, uint16_t threshold) {
  if(FREEZE_TOPOLOGY && orpl_up_only == 0) {
    return time_elapsed() > UPDATE_BLOOM_MIN_TIME && orpl_broadcast_count >= 4 && (100*count/orpl_broadcast_count >= threshold);
  } else {
    return orpl_broadcast_count >= 4 && (100*count/orpl_broadcast_count >= threshold);
  }
}

void
received_noip() {
  packetbuf_copyto(&bloom_broadcast);
  bloom_received(&bloom_broadcast);
}

//static void
//bloom_udp_received(struct simple_udp_connection *c,
//         const uip_ipaddr_t *sender_addr,
//         uint16_t sender_port,
//         const uip_ipaddr_t *receiver_addr,
//         uint16_t receiver_port,
//         const uint8_t *data,
//         uint16_t datalen)
//{
//  bloom_received((struct bloom_broadcast_s *)data);
//}

void
bloom_received(struct bloom_broadcast_s *data)
{
  if(data->magic != BLOOM_MAGIC) {
    printf("Bloom received with wrong magic number\n");
    return;
  }

  uint16_t neighbor_id = node_id_from_rimeaddr(packetbuf_addr(PACKETBUF_ADDR_SENDER));
  if(neighbor_id == 0) {
    return;
  }
  uint16_t neighbor_rank = data->rank;

  /* EDC: store rank as neighbor attribute, update metric */
  uint16_t rank_before = rpl_get_parent_rank_default((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER), 0xffff);
  printf("Bloom: received rank from %u %u -> %u (%p)\n", neighbor_id, rank_before, neighbor_rank, data);

  rpl_set_parent_rank((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER), neighbor_rank);
  rpl_recalculate_ranks();

  uint16_t count = rpl_get_parent_bc_ackcount_default((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER), 0xffff);
  if(count == 0xffff) {
    return;
  }

  if(orpl_up_only == 0) {
    rpl_rank_t curr_edc = orpl_current_edc();
    uip_ipaddr_t sender_ipaddr;
    /* Merge Bloom sets */
    if(neighbor_rank != 0xffff && neighbor_rank > EDC_W && (neighbor_rank - EDC_W) > curr_edc && test_prr(count, NEIGHBOR_PRR_THRESHOLD)) {
      set_ipaddr_from_id(&sender_ipaddr, neighbor_id);
      int bit_count_before = routing_set_count_bits();
      if(is_node_addressable(&sender_ipaddr)) {
        routing_set_insert(&sender_ipaddr);
        printf("Bloom: inserting %u (%u<%u, %u/%lu, %u->%u) (%s)\n", neighbor_id, curr_edc, neighbor_rank, count, orpl_broadcast_count, bit_count_before, routing_set_count_bits(), "bloom received");
      }
      routing_set_merge(((struct bloom_broadcast_s*)data)->filter, neighbor_id);
      int bit_count_after = routing_set_count_bits();
      printf("Bloom: merging filter from %u (%u<%u, %u/%lu, %u->%u)\n", neighbor_id, curr_edc, neighbor_rank, count, orpl_broadcast_count, bit_count_before, bit_count_after);
      if(curr_instance && bit_count_after != bit_count_before) {
        printf("Anycast: reset DIO timer (bloom received)\n");
        //      bit_count_last = bit_count_after;
        //      rpl_reset_dio_timer(curr_instance);
        bloom_request_broadcast();
      }
      routing_set_merged_count++;
    }
  }
}

void anycast_add_neighbor_to_bloom(rimeaddr_t *neighbor_addr, const char *message) {
  uip_ipaddr_t neighbor_ipaddr;
  uint16_t neighbor_id = node_id_from_rimeaddr(neighbor_addr);
  uint16_t count = rpl_get_parent_bc_ackcount_default((uip_lladdr_t *)neighbor_addr, 0xffff);
  rpl_rank_t curr_edc = orpl_current_edc();
  if(count == 0xffff) {
    return;
  }
  uint16_t neighbor_rank = rpl_get_parent_rank_default((uip_lladdr_t *)neighbor_addr, 0xffff);
  printf("Bloom: nbr rank %u\n", neighbor_rank);
  if(neighbor_rank != 0xffff
#if (ALL_NEIGHBORS_IN_FILTER==0)
      && neighbor_rank > (curr_edc + EDC_W)
#endif
      ) {
    set_ipaddr_from_id(&neighbor_ipaddr, neighbor_id);
    if(test_prr(count, NEIGHBOR_PRR_THRESHOLD)) {
      if(is_node_addressable(&neighbor_ipaddr)) {
        int bit_count_before = routing_set_count_bits();
        routing_set_insert(&neighbor_ipaddr);
        int bit_count_after = routing_set_count_bits();
        printf("Bloom: inserting %u (%u<%u, %u/%lu, %u->%u) (%s)\n", neighbor_id, curr_edc, neighbor_rank, count, orpl_broadcast_count, bit_count_before, bit_count_after, message);
      }
    }
  }
}

static void
bloom_packet_sent(void *ptr, int status, int transmissions)
{
  if(status == MAC_TX_COLLISION) {
    bloom_broacast_failed();
   }
  rpl_link_neighbor_callback(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), status, transmissions);
  check_neighbors();
}

void bloom_do_broadcast(void *ptr) {
  if(FREEZE_TOPOLOGY && orpl_up_only && time_elapsed() <= UPDATE_BLOOM_MIN_TIME) {
    printf("Bloom size %u\n", sizeof(struct bloom_broadcast_s));
    printf("Bloom: requesting broadcast\n");
    ctimer_set(&broadcast_bloom_timer, random_rand() % (32 * CLOCK_SECOND), bloom_do_broadcast, NULL);
  } else {
    rpl_rank_t curr_edc = orpl_current_edc();
    /* Broadcast filter */
    last_broadcasted_rank = curr_edc;
    bloom_broadcast.magic = BLOOM_MAGIC;
    bloom_broadcast.rank = curr_edc;
    memcpy(bloom_broadcast.filter, *routing_set_get_active(), sizeof(routing_set));
    sending_bloom = 1;

    printf("Bloom: do broadcast %u\n", bloom_broadcast.rank);
    packetbuf_clear();
    packetbuf_copyfrom(&bloom_broadcast, sizeof(struct bloom_broadcast_s));
    packetbuf_set_addr(PACKETBUF_ADDR_RECEIVER, &rimeaddr_null);
    /* We use frame pending bit to tell that this is a no-IP packet containing a Bloom filter */
    packetbuf_set_attr(PACKETBUF_ATTR_PENDING, 1);
    NETSTACK_MAC.send(&bloom_packet_sent, NULL);

    sending_bloom = 0;
  }
}

void bloom_broacast_failed() {
  bloom_request_broadcast();
}

void bloom_request_broadcast() {
  printf("Bloom: requesting broadcast\n");
  ctimer_set(&broadcast_bloom_timer, random_rand() % (4 * NETSTACK_RDC.channel_check_interval()), bloom_do_broadcast, NULL);
}

void
orpl_trickle_callback(rpl_instance_t *instance) {
  ORPL_LOG_NULL("Anycast: trickle callback");
  curr_instance = instance;
  curr_dag = instance ? instance->current_dag : NULL;

  if(orpl_up_only == 0) {
    check_neighbors();

#if !FREEZE_TOPOLOGY
    /* Bloom filter ageing */
    printf("Bloom: swapping\n");
    routing_set_swap();
#endif

    bloom_request_broadcast();

    //  int bit_count_current = routing_set_count_bits();
    //  if(curr_instance && bit_count_current != bit_count_last) {
    //    printf("Anycast: reset DIO timer (trickle callback)\n");
    //    rpl_reset_dio_timer(curr_instance);
    //    bit_count_last = bit_count_current;
    //  }

  }

  /* We recalculate the ranks periodically */
  rpl_recalculate_ranks();
}

void
orpl_update_edc(rpl_rank_t edc)
{
  rpl_rank_t curr_edc = orpl_current_edc();
  rpl_dag_t *dag = rpl_get_any_dag();

  if(dag) {
    dag->rank = edc;
  }
  printf("ORPL: updating rank %p %u\n", dag, dag->rank);

  /* Reset DIO timer if the rank changed significantly */
  if(curr_instance && last_broadcasted_rank != 0xffff &&
      (
      (last_broadcasted_rank > curr_edc && last_broadcasted_rank - curr_edc > RANK_MAX_CHANGE)
      ||
      (curr_edc > last_broadcasted_rank && curr_edc - last_broadcasted_rank > RANK_MAX_CHANGE)
      )) {
    PRINTF("ORPL: reset DIO timer (rank changed from %u to %u)\n", last_broadcasted_rank, curr_edc);
    last_broadcasted_rank = curr_edc;
    rpl_reset_dio_timer(curr_instance);
  }

  if(edc != curr_edc) {
    ANNOTATE("#A rank=%u.%u\n", edc/EDC_DIVISOR,
        (10 * (edc % EDC_DIVISOR)) / EDC_DIVISOR);
  }

  curr_edc = edc;
}

void anycast_init(const uip_ipaddr_t *global_ipaddr, int is_root, int up_only) {

  orpl_anycast_init(global_ipaddr);

  start_time = RTIMER_NOW();

  orpl_up_only = up_only;

  is_edc_root = is_root;

  if(is_edc_root) {
    ANNOTATE("#A color=red\n");
    ANNOTATE("#A rank=0.0\n");
    orpl_update_edc(0);
  }
  routing_set_init();
//  uip_create_linklocal_allnodes_mcast(&bloom_addr);
//  simple_udp_register(&bloom_connection, UDP_PORT,
//                        NULL, UDP_PORT,
//                        bloom_udp_received);

}


void
anycast_packet_received() {
  uint16_t neighbor_edc = packetbuf_attr(PACKETBUF_ATTR_EDC);
  if(neighbor_edc != 0xffff) {
	  rpl_set_parent_rank((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER), neighbor_edc);
  }
}

void
broadcast_acked(const rimeaddr_t *receiver) {
  uint16_t neighbor_id = node_id_from_rimeaddr(receiver);
  if(neighbor_id != 0) {
    uint16_t count = rpl_get_parent_bc_ackcount_default((uip_lladdr_t *)receiver, 0) + 1;
    if(count > orpl_broadcast_count+1) {
      count = orpl_broadcast_count+1;
    }
    rpl_set_parent_bc_ackcount((uip_lladdr_t *)receiver, count);
  }
}

void
check_neighbors() {
  if(orpl_up_only == 0) {
    rpl_parent_t *p;
    for(p = nbr_table_head(rpl_parents);
          p != NULL;
          p = nbr_table_next(rpl_parents, p)) {
      uint16_t neighbor_id = node_id_from_rimeaddr(nbr_table_get_lladdr(rpl_parents, p));
      if(neighbor_id != 0) {
        anycast_add_neighbor_to_bloom(nbr_table_get_lladdr(rpl_parents, p), "broadcast done");
      }
    }
  }
}

void
broadcast_done() {
  printf("Anycast: broadcast done\n");
  orpl_broadcast_count++;
}

int
is_reachable_neighbor(uip_ipaddr_t *ipv6) {
  rpl_parent_t *p;
  uint16_t id = node_id_from_ipaddr(ipv6);
  for(p = nbr_table_head(rpl_parents);
			p != NULL;
			p = nbr_table_next(rpl_parents, p)) {
    uint16_t neighbor_id = node_id_from_rimeaddr(nbr_table_get_lladdr(rpl_parents, p));
    if(id == neighbor_id) {
      uint16_t count = p->bc_ackcount;
      if(count == -1) {
      	count = 0;
      }
      return test_prr(count, NEIGHBOR_PRR_THRESHOLD);
    }
  }
  return 0;
}

int
is_in_subdodag(uip_ipaddr_t *ipv6) {
  return is_node_addressable(ipv6) && routing_set_contains(ipv6);
}

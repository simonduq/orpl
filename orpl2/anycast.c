#include "deployment.h"
#include "cooja-debug.h"
#include "anycast.h"
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
#include "cc2420-softack.h"
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

#define RANK_MAX_CHANGE (2*EDC_DIVISOR)

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

static void orpl_softack_acked_callback(const uint8_t *buf, uint8_t len);
static void orpl_softack_input_callback(const uint8_t *buf, uint8_t len, uint8_t **ackbufptr, uint8_t *acklen);
void rpl_link_neighbor_callback(const rimeaddr_t *addr, int status, int numtx);

int forwarder_set_size = 0;
int neighbor_set_size = 0;
static rtimer_clock_t start_time;
static uip_ipaddr_t prefix;

int time_elapsed() {
  return (RTIMER_NOW()-start_time)/(RTIMER_ARCH_SECOND*60);
}

static int orpl_up_only = 0;

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

enum anycast_direction_e {
  direction_none,
  direction_up,
  direction_down,
  direction_nbr,
  direction_recover
};

rimeaddr_t anycast_addr_up = {.u8 = {0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa}};
rimeaddr_t anycast_addr_down = {.u8 = {0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfb}};
rimeaddr_t anycast_addr_nbr = {.u8 = {0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc}};
rimeaddr_t anycast_addr_recover = {.u8 = {0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd}};
uint16_t hbh_edc = EDC_DIVISOR;
uint16_t e2e_edc = 0xffff;
uint32_t routing_set_merged_count = 0;
uint32_t anycast_count_incomming;
uint32_t anycast_count_acked;

int sending_bloom = 0;
int is_edc_root = 0;

uint32_t broadcast_count = 0;

static struct bloom_broadcast_s bloom_broadcast;
static struct acked_down acked_down[ACKED_DOWN_SIZE];
static uint16_t last_broadcasted_rank = 0xffff;
static rpl_dag_t *curr_dag;
static rpl_instance_t *curr_instance;
static uint32_t curr_ackcount_edc_sum;
static uint32_t curr_ackcount_sum;
static struct ctimer broadcast_bloom_timer;
//static int bit_count_last = 0;

void check_neighbors();
int test_prr(uint16_t count, uint16_t threshold);
void bloom_received(struct bloom_broadcast_s *data);
void bloom_request_broadcast();

int is_node_addressable(uip_ipaddr_t *ipv6) {
  return 1;
}

/*---------------------------------------------------------------------------*/
void update_annotations() {
  if(e2e_edc != 0xffff) {
	ANNOTATE("#A rank=%u.%u\n", e2e_edc/EDC_DIVISOR,
	       (10 * (e2e_edc % EDC_DIVISOR)) / EDC_DIVISOR);
  }
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
//void blacklist_insert_dst(uint16_t id) {
//  printf("Bloom: blacklisting node %u\n", id);
//  int i;
//  for(i = BLACKLIST_SIZE - 1; i > 0; --i) {
//    blacklisted_seqnos[i] = blacklisted_seqnos[i - 1];
//  }
//  blacklisted_seqnos[0] = seqno;
//}

int blacklist_contains(uint32_t seqno) {
  int i;
  for(i = 0; i < BLACKLIST_SIZE; ++i) {
    if(seqno == blacklisted_seqnos[i]) {
      return 1;
    }
  }
  return 0;
}
//int blacklist_contains_dst(uint16_t id) {
//  int i;
//  for(i = 0; i < BLACKLIST_SIZE; ++i) {
//    if(seqno == blacklisted_seqnos[i]) {
//      return 1;
//    }
//  }
//  return 0;
//}

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
      printf("Ackcount: [%u] %u/%lu (%u %u -> %u) ->", neighbor_id, count, broadcast_count, e2e_edc, neighbor_rank,
          (neighbor_rank != 0xffff && neighbor_rank > e2e_edc && test_prr(count, NEIGHBOR_PRR_THRESHOLD))?1:0);
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
    return time_elapsed() > UPDATE_BLOOM_MIN_TIME && broadcast_count >= 4 && (100*count/broadcast_count >= threshold);
  } else {
    return broadcast_count >= 4 && (100*count/broadcast_count >= threshold);
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
  update_e2e_edc(0);

  uint16_t count = rpl_get_parent_bc_ackcount_default((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER), 0xffff);
  if(count == 0xffff) {
    return;
  }

  if(orpl_up_only == 0) {
    uip_ipaddr_t sender_ipaddr;
    /* Merge Bloom sets */
    if(neighbor_rank != 0xffff && neighbor_rank > EDC_W && (neighbor_rank - EDC_W) > e2e_edc && test_prr(count, NEIGHBOR_PRR_THRESHOLD)) {
      set_ipaddr_from_id(&sender_ipaddr, neighbor_id);
      int bit_count_before = routing_set_count_bits();
      if(is_node_addressable(&sender_ipaddr)) {
        routing_set_insert(&sender_ipaddr);
        printf("Bloom: inserting %u (%u<%u, %u/%lu, %u->%u) (%s)\n", neighbor_id, e2e_edc, neighbor_rank, count, broadcast_count, bit_count_before, routing_set_count_bits(), "bloom received");
      }
      routing_set_merge(((struct bloom_broadcast_s*)data)->filter, neighbor_id);
      int bit_count_after = routing_set_count_bits();
      printf("Bloom: merging filter from %u (%u<%u, %u/%lu, %u->%u)\n", neighbor_id, e2e_edc, neighbor_rank, count, broadcast_count, bit_count_before, bit_count_after);
      if(curr_instance && bit_count_after != bit_count_before) {
        printf("Anycast: reset DIO timer (bloom received)\n");
        //      bit_count_last = bit_count_after;
        //      rpl_reset_dio_timer(curr_instance);
        bloom_request_broadcast();
      }
      routing_set_merged_count++;
    }
    update_annotations();
  }
}

void anycast_add_neighbor_to_bloom(rimeaddr_t *neighbor_addr, const char *message) {
  uip_ipaddr_t neighbor_ipaddr;
  uint16_t neighbor_id = node_id_from_rimeaddr(neighbor_addr);
  uint16_t count = rpl_get_parent_bc_ackcount_default((uip_lladdr_t *)neighbor_addr, 0xffff);
  if(count == 0xffff) {
    return;
  }
  uint16_t neighbor_rank = rpl_get_parent_rank_default((uip_lladdr_t *)neighbor_addr, 0xffff);
  if(neighbor_rank != 0xffff
#if (ALL_NEIGHBORS_IN_FILTER==0)
      && neighbor_rank > (e2e_edc + EDC_W)
#endif
      ) {
    set_ipaddr_from_id(&neighbor_ipaddr, neighbor_id);
    if(test_prr(count, NEIGHBOR_PRR_THRESHOLD)) {
      if(is_node_addressable(&neighbor_ipaddr)) {
        int bit_count_before = routing_set_count_bits();
        routing_set_insert(&neighbor_ipaddr);
        int bit_count_after = routing_set_count_bits();
        printf("Bloom: inserting %u (%u<%u, %u/%lu, %u->%u) (%s)\n", neighbor_id, e2e_edc, neighbor_rank, count, broadcast_count, bit_count_before, bit_count_after, message);
      }
    }
  }
  update_annotations();
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
    /* Broadcast filter */
    last_broadcasted_rank = e2e_edc;
    bloom_broadcast.magic = BLOOM_MAGIC;
    bloom_broadcast.rank = e2e_edc;
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

  update_e2e_edc(1);
  update_annotations();
}

void anycast_init(const uip_ipaddr_t *global_ipaddr, int is_root, int up_only) {

  cc2420_softack_subscribe(orpl_softack_input_callback, orpl_softack_acked_callback);
  uip_ip6addr(&prefix, 0, 0, 0, 0, 0, 0, 0, 0);
  memcpy(&prefix, global_ipaddr, 8);

  start_time = RTIMER_NOW();

  orpl_up_only = up_only;

  is_edc_root = is_root;
  if(is_edc_root) {
	ANNOTATE("#A color=red\n");
    e2e_edc = 0;
  }
  routing_set_init();
//  uip_create_linklocal_allnodes_mcast(&bloom_addr);
//  simple_udp_register(&bloom_connection, UDP_PORT,
//                        NULL, UDP_PORT,
//                        bloom_udp_received);

}

void anycast_set_packetbuf_addr() {
  uint16_t *ptr = (uint16_t *)packetbuf_addr(PACKETBUF_ADDR_RECEIVER);
  if(rimeaddr_cmp((rimeaddr_t*)ptr, &anycast_addr_up) || rimeaddr_cmp((rimeaddr_t*)ptr, &anycast_addr_down)
      || rimeaddr_cmp((rimeaddr_t*)ptr, &anycast_addr_nbr) || rimeaddr_cmp((rimeaddr_t*)ptr, &anycast_addr_recover)) {
    struct app_data data;
    appdata_copy(&data, appdataptr_from_packetbuf());
    ptr[1] = e2e_edc;
    ptr[2] = data.seqno >> 16;
    ptr[3] = data.seqno;
  }
}

int anycast_parse_addr(rimeaddr_t *addr, enum anycast_direction_e *anycast_direction, uint16_t *e2e_edc, uint32_t *seqno) {
  int up = 0;
  int down = 0;
  int nbr = 0;
  int recover = 0;

  int i;
  uint8_t reverted_addr[8];
  for(i=0; i<8; i++) {
    reverted_addr[i] = addr->u8[7-i];
  }

  /* Compare only the 2 first bytes, as other bytes carry e2e_edc and seqno */
  if(!memcmp(reverted_addr, &anycast_addr_up, 2)) {
    if(anycast_direction) *anycast_direction = direction_up;
    up = 1;
  } else if(!memcmp(reverted_addr, &anycast_addr_down, 2)) {
    if(anycast_direction) *anycast_direction = direction_down;
    down = 1;
  } else if(!memcmp(reverted_addr, &anycast_addr_nbr, 2)) {
    if(anycast_direction) *anycast_direction = direction_nbr;
    nbr = 1;
  } else if(!memcmp(reverted_addr, &anycast_addr_recover, 2)) {
    if(anycast_direction) *anycast_direction = direction_recover;
    recover = 1;
  }

  uint16_t *ptr = (uint16_t*)reverted_addr;
  if(e2e_edc) *e2e_edc = ptr[1];
  if(seqno) *seqno = (((uint32_t)ptr[2]) << 16) + (uint32_t)ptr[3];

  if(!up && !down && !nbr && !recover) {
    return 0;
  } else {
    return 1;
  }
}

static void
start_forwarder_set(int verbose) {
  curr_ackcount_sum = 0;
  curr_ackcount_edc_sum = 0;

  if(verbose) {
    printf("EDC: starting calculation. hbh_edc: %u, e2e_edc %u\n", hbh_edc, e2e_edc);
  }
  e2e_edc = 0xffff;
}

static int
add_to_forwarder_set(rpl_parent_t *curr_min, uint16_t curr_min_rank, uint16_t ackcount, int verbose) {
  uint16_t tentative;
  uint32_t total_tx_count;

  if(ackcount > broadcast_count) {
    ackcount = broadcast_count;
  }

  total_tx_count = broadcast_count;
  if(total_tx_count == 0) {
    total_tx_count = 1;
  }

  curr_ackcount_sum += ackcount;
  curr_ackcount_edc_sum += ackcount * curr_min_rank;

  uint32_t A = hbh_edc * total_tx_count / curr_ackcount_sum;
  uint32_t B = curr_ackcount_edc_sum / curr_ackcount_sum;
  if(verbose) {
    printf("-- A: %5lu, B: %5lu (%u/%lu) ",
          A,
          B,
          ackcount,
          total_tx_count
    );
  }

  tentative = A + B + EDC_W;

  if(verbose) {
    printf("EDC %5u ", tentative);
  }
  if(tentative < e2e_edc) {
    e2e_edc = tentative;
    return 1;
  } else {
    return 0;
  }
}

/* Compute forwarder set with minimal EDC */
void
update_e2e_edc(int verbose) {

  if(FREEZE_TOPOLOGY && orpl_up_only == 0) {
    if(time_elapsed() > UPDATE_EDC_MAX_TIME) {
      return;
    }
  }

  static uint16_t prev_e2e_edc;
  prev_e2e_edc = e2e_edc;
  forwarder_set_size = 0;
  neighbor_set_size = 0;

  if(is_edc_root) {
    e2e_edc = 0;
  } else {
    rpl_parent_t *p;
    int index;

    int curr_index = 0;
    rpl_parent_t *curr_min;
    uint16_t curr_min_rank = 0xffff;
    uint16_t curr_min_ackcount = 0xffff;

    int prev_index = -1;
    rpl_parent_t *prev_min = NULL;
    uint16_t prev_min_rank = 0;

    start_forwarder_set(verbose);

    /* Loop on the parents ordered by increasing rank */
    do {
      curr_min = NULL;

      for(p = nbr_table_head(rpl_parents), index = 0;
            p != NULL;
            p = nbr_table_next(rpl_parents, p), index++) {
        uint16_t rank = p->rank;
        uint16_t ackcount = p->bc_ackcount;
        uint16_t neighbor_id = node_id_from_rimeaddr(nbr_table_get_lladdr(rpl_parents,p));

        if(neighbor_id != 0
            && rank != 0xffff
            && ackcount != 0
            && (curr_min == NULL || rank < curr_min_rank)
            && (rank > prev_min_rank || (rank == prev_min_rank && index > prev_index))
        ) {
          curr_index = index;
          curr_min = p;
          curr_min_rank = rank;
          curr_min_ackcount = ackcount;
        }
      }
      /* Here, curr_min contains the current p in our ordered lookup */
      if(curr_min) {
        uint16_t curr_id = node_id_from_rimeaddr(nbr_table_get_lladdr(rpl_parents,curr_min));
        if(verbose) printf("EDC: -> node %3u rank: %5u ", curr_id, curr_min_rank);
        neighbor_set_size++;
        if(add_to_forwarder_set(curr_min, curr_min_rank, curr_min_ackcount, verbose) == 1) {
          forwarder_set_size++;
          if(verbose) printf("*\n");
          ANNOTATE("#L %u 1\n", curr_id);
        } else {
          if(verbose) printf("\n");
          ANNOTATE("#L %u 0\n", curr_id);
        }
        prev_index = curr_index;
        prev_min = curr_min;
        prev_min_rank = curr_min_rank;
      }
    } while(curr_min != NULL);

    if(verbose) printf("EDC: final %u\n", e2e_edc);
  }

  if(e2e_edc != prev_e2e_edc) {
	update_annotations();
//    printf("Anycast: updated edc: e2e %u hbh %u fs %u\n", e2e_edc, hbh_edc, forwarder_set_size);
  }

  if(curr_dag) {
	  curr_dag->rank = e2e_edc;
  }

  /* Reset DIO timer if the rank changed significantly */
  if(curr_instance && last_broadcasted_rank != 0xffff &&
		  (
			(last_broadcasted_rank > e2e_edc && last_broadcasted_rank - e2e_edc > RANK_MAX_CHANGE)
			||
			(e2e_edc > last_broadcasted_rank && e2e_edc - last_broadcasted_rank > RANK_MAX_CHANGE)
		  )) {
	  printf("Anycast: reset DIO timer (rank changed from %u to %u)\n", last_broadcasted_rank, e2e_edc);
	  last_broadcasted_rank = e2e_edc;
	  rpl_reset_dio_timer(curr_instance);
  }

}

void
anycast_packet_sent() {
#define ALPHA 9
  if(FREEZE_TOPOLOGY && orpl_up_only == 0) {
    if(time_elapsed() > UPDATE_EDC_MAX_TIME) {
      return;
    }
  }
  if(packetbuf_attr(PACKETBUF_ATTR_GOING_UP)) {
    /* Calculate hop-by-hop EDC (only for up traffic) */
    uint16_t curr_hbh_edc = packetbuf_attr(PACKETBUF_ATTR_EDC);
    uint16_t weighted_curr_hbh_edc;
    uint16_t hbh_edc_old = hbh_edc;
    if(curr_hbh_edc == 0xffff) { /* this means noack, use a more aggressive alpha */
      weighted_curr_hbh_edc = EDC_DIVISOR * 2 * forwarder_set_size;
      hbh_edc = (hbh_edc * 5 + weighted_curr_hbh_edc * 5) / 10;
    } else {
      weighted_curr_hbh_edc = curr_hbh_edc * forwarder_set_size;
      hbh_edc = ((hbh_edc * ALPHA) + (weighted_curr_hbh_edc * (10-ALPHA))) / 10;
    }

    printf("Anycast: updated hbh_edc %u -> %u (%u %u)\n", hbh_edc_old, hbh_edc, curr_hbh_edc, weighted_curr_hbh_edc);

      /* Calculate end-to-end EDC */
    update_e2e_edc(1);
  }
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
    if(count > broadcast_count+1) {
      count = broadcast_count+1;
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
  broadcast_count++;
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

/**********************************************************************
 ************ Softack-related code ************************************
 *********************************************************************/

/* A buffer where extended 802.15.4 are prepared */
static unsigned char ackbuf[3 + EXTRA_ACK_LEN] = {0x02, 0x00};
/* Seqno of the last acked frame */
static uint8_t last_acked_seqno = -1;

/* The frame was acked (i.e. we wanted to ack it AND it was not corrupt).
 * Store the last acked sequence number to avoid repeatedly acking in case
 * we're not duty cycled (e.g. border router) */
static void
orpl_softack_acked_callback(const uint8_t *frame, uint8_t framelen) {
	last_acked_seqno = frame[2];
}

/* Called for every incoming frame from interrupt. We check if we want to ack the
 * frame and prepare an ACK if needed */
static void
orpl_softack_input_callback(const uint8_t *frame, uint8_t framelen, uint8_t **ackbufptr, uint8_t *acklen) {
	uint8_t fcf, is_data, ack_required, seqno;
	int do_ack = 0;

	fcf = frame[0];
	is_data = (fcf & 7) == 1;
	ack_required = (fcf >> 5) & 1;
	seqno = frame[2];

	if(is_data) {
		if(ack_required) { /* This is unicast or unicast, parse it */
			do_ack = orpl_parse_802154_frame((uint8_t *)frame, framelen, NULL) & DO_ACK;
		} else { /* We also ack broadcast, even if we didn't modify the framer
		and still send them with ack_required unset */
			if(seqno != last_acked_seqno) {
				do_ack = 1;
			}
		}
	}

	if(do_ack) { /* Prepare ack */
		*ackbufptr = ackbuf;
		*acklen = sizeof(ackbuf);
		ackbuf[2] = seqno;
		/* Append our address to the standard 802.15.4 ack */
		rimeaddr_copy((rimeaddr_t*)(ackbuf+3), &rimeaddr_node_addr);
		/* Append our rank to the ack */
		ackbuf[3+8] = e2e_edc & 0xff;
		ackbuf[3+8+1] = (e2e_edc >> 8)& 0xff;
	} else {
		*acklen = 0;
	}
}

/* Parse a modified 802.15.4 frame, extract the neighbor EDC, and return
 * information regarding routing and software acks, described as a subset of the
 * flags DO_ACK, IS_ANYCAST, FROM_SUBDODAG, IS_RECOVERY. */
uint8_t
orpl_parse_802154_frame(uint8_t *data, uint8_t len, uint16_t *neighbor_edc)
{
  frame802154_fcf_t fcf;
  uint8_t *dest_addr = NULL;
  uint8_t *src_addr = NULL;
  int do_ack = 0;
  int is_anycast = 0;
  int from_subdodag = 0;
  int recovery = 0;
  int i;

  if(len < 3) {
    return 0;
  }

  /* Decode the FCF */
  fcf.frame_type = data[0] & 7;
  fcf.ack_required = (data[0] >> 5) & 1;
  dest_addr = data + 3 + 2;
  src_addr = data + 3 + 2 + 8;

  /* This is a unciast or anycast data frame */
  if(fcf.frame_type == FRAME802154_DATAFRAME && fcf.ack_required == 1) {
    enum anycast_direction_e anycast_direction = direction_none;
    uint32_t seqno;
    uint8_t src_addr_host_order[8];
    uint8_t dest_addr_host_order[8];
    uint16_t current_edc;

    /* Convert from 802.15.4 little endian to Contiki's big-endian addresses */
    for(i=0; i<8; i++) {
      src_addr_host_order[i] = src_addr[7-i];
      src_addr_host_order[i] = dest_addr[7-i];
    }
    /* TODO ORPL: should use address instead of id */
    uint16_t neighbor_id = node_id_from_rimeaddr((rimeaddr_t*)src_addr_host_order);

    /* Parse the destination address */
    if(anycast_parse_addr((rimeaddr_t*)dest_addr, &anycast_direction, &current_edc, &seqno)) {

      /* This is anycast, make forwarding decision */
      is_anycast = IS_ANYCAST;
      if(anycast_direction == direction_up) {
        from_subdodag = FROM_SUBDODAG;
      }

      /* Calculate destination IPv6 address */
      /* TODO ORPL: better document this addressing */
      uip_ipaddr_t dest_ipv6;
      memcpy(&dest_ipv6, &prefix, 8);
      memcpy(((char*)&dest_ipv6)+8, data + 22 + 12, 8);

      if(uip_ds6_is_my_addr(&dest_ipv6)) {
        /* Take the data if it is for us */
        do_ack = DO_ACK;
      } else if(rimeaddr_cmp((rimeaddr_t*)dest_addr_host_order, &rimeaddr_node_addr)) {
        /* Unicast, for us */
        do_ack = 1;
      } else if(anycast_direction == direction_up) {
        /* Routing upwards. ACK if our rank is better. */
        if(current_edc > EDC_W && e2e_edc < current_edc - EDC_W) {
          do_ack = DO_ACK;
        } else {
          /* We don't route upwards, now check if we are a common ancester of the source
           * and destination. We do this by checking our routing set against the destination. */
          if(!blacklist_contains(seqno) && is_in_subdodag(&dest_ipv6)) {
            /* Traffic is going up but we have destination in our routing set.
             * Ack it and start routing downwards (towards the destination) */
            do_ack = DO_ACK;
          }
        }
      } else if(anycast_direction == direction_down) {
        /* Routing downwards. ACK if we have a worse rank and destination is in subdodag */
        if(e2e_edc > EDC_W && e2e_edc - EDC_W > current_edc
            && !blacklist_contains(seqno) && is_in_subdodag(&dest_ipv6)) {
          do_ack = DO_ACK;
        }
      } else if(anycast_direction == direction_recover) {
        /* This packet is sent back from a child that experiences false positive. Only
         * the nodes that did forward this same packet downwards before are allowed to
         * take the packet back during a recovery, before sending down again. This is
         * to avoid duplicates during the recovery process. */
        recovery = IS_RECOVERY;
        /* ORPL TODO: base this on address rather than ID */
        do_ack = acked_down_contains(seqno, neighbor_id);
      }
    }

    /* Set neighbor_edc for the caller */
    if(neighbor_edc) {
      *neighbor_edc = current_edc;
    }

    /* Set destination address to ours in case we acked the packet to it doesn't
     * get dropped next */
    if(do_ack) {
      int i;
      for(i=0; i<8; i++) {
        dest_addr[i] = rimeaddr_node_addr.u8[7-i];
      }
    }
  }

  /* Return routing/acknowledging information */
  return do_ack | is_anycast | from_subdodag | recovery;
}

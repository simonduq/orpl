#include "orpl.h"
#include "orpl-anycast.h"
#include "orpl-routing-set.h"
#include "tools/deployment.h"
#include "net/packetbuf.h"
#include "net/simple-udp.h"
#include "net/uip-ds6.h"
#include "net/rpl/rpl-private.h"
#include "lib/random.h"
#include <string.h>

#if IN_COOJA
#define DEBUG DEBUG_ANNOTATE
#else
#define DEBUG DEBUG_NONE
#endif
#include "net/uip-debug.h"

/* Insert even non-children neighbors in routing set */
#define ALL_NEIGHBORS_IN_ROUTING_SET 1
/* PRR threshold for considering a neighbor as usable */
#define NEIGHBOR_PRR_THRESHOLD 35

/* When set:
 * - stop updating EDC after N seconds
 * - start updating Routing sets only after N+1 seconds
 * - don't age routing sets */
#ifndef FREEZE_TOPOLOGY
#define FREEZE_TOPOLOGY 1
#endif

#if FREEZE_TOPOLOGY
#define UPDATE_EDC_MAX_TIME 1*60
#define UPDATE_ROUTING_SET_MIN_TIME 2*60
#else
#define UPDATE_EDC_MAX_TIME 0
#define UPDATE_ROUTING_SET_MIN_TIME 0
#endif

/* Rank changes of more than RANK_MAX_CHANGE trigger a trickle timer reset */
#define RANK_MAX_CHANGE (2*EDC_DIVISOR)
/* The last boradcasted EDC */
static uint16_t last_broadcasted_rank = 0xffff;

#ifdef UIP_CONF_DS6_LINK_NEIGHBOR_CALLBACK
#define LINK_NEIGHBOR_CALLBACK(addr, status, numtx) UIP_CONF_DS6_LINK_NEIGHBOR_CALLBACK(addr, status, numtx)
void LINK_NEIGHBOR_CALLBACK(const rimeaddr_t *addr, int status, int numtx);
#else
#define LINK_NEIGHBOR_CALLBACK(addr, status, numtx)
#endif /* UIP_CONF_DS6_LINK_NEIGHBOR_CALLBACK */

/* Set to 1 when only upwards routing is enabled */
static int orpl_up_only = 0;

/* UDP port used for routing set broadcasting */
#define ROUTING_SET_PORT 4444
/* UDP connection used for routing set broadcasting */
static struct simple_udp_connection routing_set_connection;
/* Multicast IP address used for routing set broadcasting */
static uip_ipaddr_t routing_set_addr;
/* Data structure used for routing set broadcasting. Also includes
 * current rank. */
struct routing_set_broadcast_s {
  uint16_t rank;
  union {
    routing_set filter;
    uint8_t padding[64];
  };
};
/* Flag used to tell bottom layers that the current UDP transmission
 * is a routing set, so that the desired callback function is called
 * after each transmission attempt */
int sending_routing_set = 0;
/* Timer for periodic broadcast of routing sets */
static struct ctimer routing_set_broadcast_timer;

/* Data structure for storing the history of packets that were
 * acked while routing downwards. Used during recovery to ensure
 * only parents that forwarded the packet down before will take
 * it back (avoids duplicates during in-depth exploration) */
struct packet_acked_down_s {
  uint32_t seqno;
  uint16_t id;
};
/* Size of the packet acked down history */
#define ACKED_DOWN_SIZE 32
/* The histrory of packets acked down */
static struct packet_acked_down_s acked_down[ACKED_DOWN_SIZE];

/* The current RPL instance */
static rpl_instance_t *curr_instance;

/* Routing set filter false positive blacklist */
#define BLACKLIST_SIZE 16
static uint32_t blacklisted_seqnos[BLACKLIST_SIZE];

/* Total number of broadcast sent */
uint32_t orpl_broadcast_count = 0;

static void check_neighbors();

/* Returns 1 if the topology is frozen, i.e. we are not allowed to change rank */
int
orpl_are_routing_set_active()
{
  return FREEZE_TOPOLOGY && orpl_up_only == 0 && clock_seconds() > UPDATE_ROUTING_SET_MIN_TIME;
}

/* Returns 1 if the topology is frozen, i.e. we are not allowed to change rank */
int
orpl_is_edc_frozen()
{
  return FREEZE_TOPOLOGY && orpl_up_only == 0 && clock_seconds() > UPDATE_EDC_MAX_TIME;
}

/* Returns 1 if the node is root of ORPL */
int
orpl_is_root()
{
  return orpl_current_edc() == 0;
}

/* Returns current EDC of the node */
rpl_rank_t
orpl_current_edc()
{
  rpl_dag_t *dag = rpl_get_any_dag();
  return dag == NULL ? 0xffff : dag->rank;
}

/* Returns 1 if the number of broadcast acked by a neighbor (count) is high enough
 * to consider a neighbor as usable */
static int
test_neighbor_prr(uint16_t count)
{
  return orpl_broadcast_count >= 4 && (100*count/orpl_broadcast_count >= NEIGHBOR_PRR_THRESHOLD);
}

/* Returns 1 if addr is the ip of a reachable neighbor */
int
is_reachable_neighbor(uip_ipaddr_t *ipaddr)
{
  uint16_t count = rpl_get_parent_bc_ackcount_default(
      uip_ds6_nbr_lladdr_from_ipaddr(ipaddr), 0);
  return count != 0 && test_neighbor_prr(count);
}

/* Insert a packet sequence number to the blacklist
 * (used for false positive recovery) */
void
orpl_blacklist_insert(uint32_t seqno)
{
  ORPL_LOG("ORPL: blacklisting %lx\n", seqno);
  int i;
  for(i = BLACKLIST_SIZE - 1; i > 0; --i) {
    blacklisted_seqnos[i] = blacklisted_seqnos[i - 1];
  }
  blacklisted_seqnos[0] = seqno;
}

/* Returns 1 is the sequence number is contained in the blacklist */
int
blacklist_contains(uint32_t seqno)
{
  int i;
  for(i = 0; i < BLACKLIST_SIZE; ++i) {
    if(seqno == blacklisted_seqnos[i]) {
      return 1;
    }
  }
  return 0;
}

/* A packet was routed downwards successfully, insert it into our
 * history. Used during false positive recovery. */
void
acked_down_insert(uint32_t seqno, uint16_t id)
{
  ORPL_LOG("ORPL: inserted ack down %lx %u\n", seqno, id);
  int i;
  for(i = ACKED_DOWN_SIZE - 1; i > 0; --i) {
    acked_down[i] = acked_down[i - 1];
  }
  acked_down[0].seqno = seqno;
  acked_down[0].id = id;
}

/* Returns 1 if a given packet is in the acked down history */
int
acked_down_contains(uint32_t seqno, uint16_t id)
{
  int i;
  for(i = 0; i < ACKED_DOWN_SIZE; ++i) {
    if(seqno == acked_down[i].seqno && id == acked_down[i].id) {
      return 1;
    }
  }
  return 0;
}

static void
routing_set_do_broadcast(void *ptr)
{
  if(FREEZE_TOPOLOGY && orpl_up_only && clock_seconds() <= UPDATE_ROUTING_SET_MIN_TIME) {
    ORPL_LOG("Routing set: requesting broadcast\n");
    ctimer_set(&routing_set_broadcast_timer, random_rand() % (32 * CLOCK_SECOND), routing_set_do_broadcast, NULL);
  } else {
    struct routing_set_broadcast_s routing_set_broadcast;
    rpl_rank_t curr_edc = orpl_current_edc();
    /* Broadcast filter */
    last_broadcasted_rank = curr_edc;
    routing_set_broadcast.rank = curr_edc;
    memcpy(routing_set_broadcast.filter, *orpl_routing_set_get_active(), sizeof(routing_set));

    ORPL_LOG("Routing set: do broadcast %u\n", routing_set_broadcast.rank);
    sending_routing_set = 1;
    simple_udp_sendto(&routing_set_connection, &routing_set_broadcast, sizeof(struct routing_set_broadcast_s), &routing_set_addr);
    sending_routing_set = 0;

  }
}

static void
routing_set_request_broadcast()
{
  ORPL_LOG("Routing set: requesting broadcast\n");
  ctimer_set(&routing_set_broadcast_timer, random_rand() % (4 * NETSTACK_RDC.channel_check_interval()), routing_set_do_broadcast, NULL);
}

static void
routing_set_received(struct simple_udp_connection *c,
         const uip_ipaddr_t *sender_addr,
         uint16_t sender_port,
         const uip_ipaddr_t *receiver_addr,
         uint16_t receiver_port,
         const uint8_t *payload,
         uint16_t datalen)
{
  struct routing_set_broadcast_s *data = (struct routing_set_broadcast_s *)payload;

  uint16_t neighbor_id = node_id_from_rimeaddr(packetbuf_addr(PACKETBUF_ADDR_SENDER));
  if(neighbor_id == 0) {
    return;
  }
  uint16_t neighbor_rank = data->rank;

  /* EDC: store rank as neighbor attribute, update metric */
  uint16_t rank_before = rpl_get_parent_rank_default((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER), 0xffff);
  ORPL_LOG("Routing set: received rank from %u %u -> %u (%p)\n", neighbor_id, rank_before, neighbor_rank, data);

  rpl_set_parent_rank((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER), neighbor_rank);
  rpl_recalculate_ranks();

  uint16_t count = rpl_get_parent_bc_ackcount_default((uip_lladdr_t *)packetbuf_addr(PACKETBUF_ADDR_SENDER), 0xffff);
  if(count == 0xffff) {
    return;
  }

  if(orpl_are_routing_set_active()) {
    rpl_rank_t curr_edc = orpl_current_edc();
    /* Merge Routing sets */
    if(neighbor_rank != 0xffff && neighbor_rank > EDC_W && (neighbor_rank - EDC_W) > curr_edc && test_neighbor_prr(count)) {
      uip_ipaddr_t sender_ipaddr;
      set_ipaddr_from_id(&sender_ipaddr, neighbor_id);
      int bit_count_before = orpl_routing_set_count_bits();
      orpl_routing_set_insert(&sender_ipaddr);
      ORPL_LOG("Routing set: inserting %u (%u<%u, %u/%lu, %u->%u) (%s)\n", neighbor_id, curr_edc, neighbor_rank, count, orpl_broadcast_count, bit_count_before, orpl_routing_set_count_bits(), "routing set received");
      orpl_routing_set_merge(((struct routing_set_broadcast_s*)data)->filter, neighbor_id);
      int bit_count_after = orpl_routing_set_count_bits();
      ORPL_LOG("Routing set: merging filter from %u (%u<%u, %u/%lu, %u->%u)\n", neighbor_id, curr_edc, neighbor_rank, count, orpl_broadcast_count, bit_count_before, bit_count_after);
      if(curr_instance && bit_count_after != bit_count_before) {
        ORPL_LOG("Anycast: reset DIO timer (routing set received)\n");
        //      bit_count_last = bit_count_after;
        //      rpl_reset_dio_timer(curr_instance);
        routing_set_request_broadcast();
      }
    }
  }
}

void
routing_set_add_neighbor(rimeaddr_t *neighbor_addr, const char *message)
{
  if(orpl_are_routing_set_active()) {

    uip_ipaddr_t neighbor_ipaddr;
    uint16_t neighbor_id = node_id_from_rimeaddr(neighbor_addr);
    uint16_t count = rpl_get_parent_bc_ackcount_default((uip_lladdr_t *)neighbor_addr, 0xffff);
    rpl_rank_t curr_edc = orpl_current_edc();

    if(count == 0xffff) {
      return;
    }

    uint16_t neighbor_rank = rpl_get_parent_rank_default((uip_lladdr_t *)neighbor_addr, 0xffff);

    ORPL_LOG("Routing set: nbr rank %u\n", neighbor_rank);

    if(neighbor_rank != 0xffff
#if (ALL_NEIGHBORS_IN_ROUTING_SET==0)
        && neighbor_rank > (curr_edc + EDC_W)
#endif
    ) {
      set_ipaddr_from_id(&neighbor_ipaddr, neighbor_id);
      if(test_neighbor_prr(count)) {
        int bit_count_before = orpl_routing_set_count_bits();
        orpl_routing_set_insert(&neighbor_ipaddr);
        int bit_count_after = orpl_routing_set_count_bits();
        ORPL_LOG("Routing set: inserting %u (%u<%u, %u/%lu, %u->%u) (%s)\n", neighbor_id, curr_edc, neighbor_rank, count, orpl_broadcast_count, bit_count_before, bit_count_after, message);
      }
    }
  }
}

void
routing_set_packet_sent(void *ptr, int status, int transmissions)
{
  if(status == MAC_TX_COLLISION) {
    routing_set_request_broadcast();
  }
  LINK_NEIGHBOR_CALLBACK(packetbuf_addr(PACKETBUF_ADDR_RECEIVER), status, transmissions);
  check_neighbors();
}

void
orpl_trickle_callback(rpl_instance_t *instance)
{
  ORPL_LOG_NULL("Anycast: trickle callback");
  curr_instance = instance;

  if(orpl_up_only == 0) {
    check_neighbors();

#if !FREEZE_TOPOLOGY
    /* Routing set filter ageing */
    ORPL_LOG("Routing set: swapping\n");
    orpl_routing_set_swap();
#endif

    routing_set_request_broadcast();

    //  int bit_count_current = orpl_routing_set_count_bits();
    //  if(curr_instance && bit_count_current != bit_count_last) {
    //    ORPL_LOG("Anycast: reset DIO timer (trickle callback)\n");
    //    rpl_reset_dio_timer(curr_instance);
    //    bit_count_last = bit_count_current;
    //  }

  }

  /* We recalculate the ranks periodically */
  rpl_recalculate_ranks();
}

static void
check_neighbors()
{
  if(orpl_up_only == 0) {
    rpl_parent_t *p;
    for(p = nbr_table_head(rpl_parents);
          p != NULL;
          p = nbr_table_next(rpl_parents, p)) {
      uint16_t neighbor_id = node_id_from_rimeaddr(nbr_table_get_lladdr(rpl_parents, p));
      if(neighbor_id != 0) {
        routing_set_add_neighbor(nbr_table_get_lladdr(rpl_parents, p), "broadcast done");
      }
    }
  }
}

void
broadcast_acked(const rimeaddr_t *receiver)
{
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
broadcast_done()
{
  ORPL_LOG("Anycast: broadcast done\n");
  orpl_broadcast_count++;
}

/* Update the current EDC (rank of the node) */
void
orpl_update_edc(rpl_rank_t edc)
{
  rpl_rank_t curr_edc = orpl_current_edc();
  rpl_dag_t *dag = rpl_get_any_dag();

  if(dag) {
    dag->rank = edc;
  }

  /* Reset DIO timer if the rank changed significantly */
  if(curr_instance && last_broadcasted_rank != 0xffff &&
      ((last_broadcasted_rank > curr_edc && last_broadcasted_rank - curr_edc > RANK_MAX_CHANGE) ||
      (curr_edc > last_broadcasted_rank && curr_edc - last_broadcasted_rank > RANK_MAX_CHANGE))) {
    PRINTF("ORPL: reset DIO timer (rank changed from %u to %u)\n", last_broadcasted_rank, curr_edc);
    last_broadcasted_rank = curr_edc;
    rpl_reset_dio_timer(curr_instance);
  }

  /* Update EDC annotation */
  if(edc != curr_edc) {
    ANNOTATE("#A rank=%u.%u\n", edc/EDC_DIVISOR,
        (10 * (edc % EDC_DIVISOR)) / EDC_DIVISOR);
  }

  /* Update EDC */
  curr_edc = edc;
}

/* ORPL initialization */
void
orpl_init(const uip_ipaddr_t *global_ipaddr, int is_root, int up_only)
{
  orpl_up_only = up_only;

  if(is_root) {
    ANNOTATE("#A color=red\n");
    ANNOTATE("#A rank=0.0\n");
    /* Set root EDC to 0 */
    orpl_update_edc(0);
  }

  /* Initializa anycast and routing set modules */
  orpl_anycast_init(global_ipaddr);
  orpl_routing_set_init();

  /* Set up multicast UDP connectoin for dissemination of routing sets */
  uip_create_linklocal_allnodes_mcast(&routing_set_addr);
  simple_udp_register(&routing_set_connection, ROUTING_SET_PORT,
                        NULL, ROUTING_SET_PORT,
                        routing_set_received);

}

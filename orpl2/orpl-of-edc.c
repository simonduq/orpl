#include "net/rpl/rpl-private.h"

#define DEBUG DEBUG_PRINT | DEBUG_ANNOTATE
#include "net/uip-debug.h"
#include "anycast.h"
#include "deployment.h"
#include "packetbuf.h"

static void reset(rpl_dag_t *);
static void neighbor_link_callback(rpl_parent_t *, int, int);
static rpl_parent_t *best_parent(rpl_parent_t *, rpl_parent_t *);
static rpl_dag_t *best_dag(rpl_dag_t *, rpl_dag_t *);
static rpl_rank_t calculate_rank(rpl_parent_t *, rpl_rank_t);
static void update_metric_container(rpl_instance_t *);

static uint32_t curr_ackcount_edc_sum;
static uint32_t curr_ackcount_sum;
uint32_t broadcast_count = 0;
extern rpl_dag_t *curr_dag;
extern rpl_instance_t *curr_instance;
extern uint16_t last_broadcasted_rank;
#define RANK_MAX_CHANGE (2*EDC_DIVISOR)

rpl_of_t rpl_of_edc = {
  reset,
  neighbor_link_callback,
  best_parent,
  best_dag,
  calculate_rank,
  update_metric_container,
  1
};

/* Constants for the ETX moving average */
#define EDC_SCALE   100
#define EDC_ALPHA   90

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

  if(orpl_is_topology_frozen()) {
    return;
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

        if(rank != 0xffff
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
    ANNOTATE("#A rank=%u.%u\n", e2e_edc/EDC_DIVISOR,
        (10 * (e2e_edc % EDC_DIVISOR)) / EDC_DIVISOR);
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

static void
reset(rpl_dag_t *sag)
{
}

/* Called after transmitting to a neighbor */
static void
neighbor_link_callback(rpl_parent_t *parent, int known, int edc)
{
  /* First check if we are allowed to change rank */
  if(orpl_is_topology_frozen()) {
    return;
  }
  /* Calculate the average hop-by-hop EDC, i.e. the average strobe time
   * required before getting our anycast ACKed. We compute this only for
   * upwards traffic, as the metric and the topology are directed to the root */
  if(packetbuf_attr(PACKETBUF_ATTR_GOING_UP)) {
    uint16_t curr_hbh_edc = packetbuf_attr(PACKETBUF_ATTR_EDC); /* The strobe time for this packet */
    uint16_t weighted_curr_hbh_edc;
    uint16_t hbh_edc_old = hbh_edc;
    if(curr_hbh_edc == 0xffff) { /* This was NOACK, use a more aggressive alpha (of 50%) */
      weighted_curr_hbh_edc = EDC_DIVISOR * 2 * forwarder_set_size;
      hbh_edc = (hbh_edc * (EDC_SCALE/2) + weighted_curr_hbh_edc * (EDC_SCALE/2)) / EDC_SCALE;
    } else {
      weighted_curr_hbh_edc = curr_hbh_edc * forwarder_set_size;
      hbh_edc = ((hbh_edc * EDC_ALPHA) + (weighted_curr_hbh_edc * (EDC_SCALE-EDC_ALPHA))) / EDC_SCALE;
    }

    PRINTF("EDC: updated hbh_edc %u -> %u (%u %u)\n", hbh_edc_old, hbh_edc, curr_hbh_edc, weighted_curr_hbh_edc);

    /* Calculate end-to-end EDC */
    update_e2e_edc(1);
  }
}

static rpl_rank_t
calculate_rank(rpl_parent_t *p, rpl_rank_t base_rank)
{
  printf("EDC: calculate rank\n");
  update_e2e_edc(0);
  return e2e_edc;
}

static rpl_dag_t *
best_dag(rpl_dag_t *d1, rpl_dag_t *d2)
{
  if(d1->grounded != d2->grounded) {
    return d1->grounded ? d1 : d2;
  }

  if(d1->preference != d2->preference) {
    return d1->preference > d2->preference ? d1 : d2;
  }

  return d1->rank < d2->rank ? d1 : d2;
}

static rpl_parent_t *
best_parent(rpl_parent_t *p1, rpl_parent_t *p2)
{
  /* With EDC, we don't need to compare parents */
  return p1;
}

static void
update_metric_container(rpl_instance_t *instance)
{
  rpl_dag_t *dag;

  instance->mc.flags = RPL_DAG_MC_FLAG_P;
  instance->mc.aggr = RPL_DAG_MC_AGGR_ADDITIVE;
  instance->mc.prec = 0;

  dag = instance->current_dag;

  if (!dag->joined) {
    /* We should probably do something here */
    return;
  }

  instance->mc.type = RPL_DAG_MC;
  instance->mc.length = sizeof(instance->mc.obj.etx);
  instance->mc.obj.etx = e2e_edc;

  PRINTF("RPL: My path EDC to the root is %u (%u.%02u)\n",
      e2e_edc,
      e2e_edc / EDC_DIVISOR,
      (e2e_edc % EDC_DIVISOR * 100) / EDC_DIVISOR
  );
}

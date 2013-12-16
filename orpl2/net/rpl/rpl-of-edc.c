#include "net/rpl/rpl-private.h"
#include "net/neighbor-info.h"

#define DEBUG DEBUG_NONE
#include "net/uip-debug.h"
#include "anycast.h"

static void reset(rpl_dag_t *);
static void parent_state_callback(rpl_parent_t *, int, int);
static rpl_parent_t *best_parent(rpl_parent_t *, rpl_parent_t *);
static rpl_dag_t *best_dag(rpl_dag_t *, rpl_dag_t *);
static rpl_rank_t calculate_rank(rpl_parent_t *, rpl_rank_t);
static void update_metric_container(rpl_instance_t *);

rpl_of_t rpl_of_edc = {
  reset,
  parent_state_callback,
  best_parent,
  best_dag,
  calculate_rank,
  update_metric_container,
  1
};

/* Reject parents that have a higher link metric than the following. */
#define MAX_LINK_METRIC			0xffff

/* Reject parents that have a higher path cost than the following. */
#define MAX_PATH_COST			0xffff

/*
 * The rank must differ more than 1/PARENT_SWITCH_THRESHOLD_DIV in order
 * to switch preferred parent.
 */
#define PARENT_SWITCH_THRESHOLD_DIV	1024

typedef uint16_t rpl_path_metric_t;

static void
reset(rpl_dag_t *sag)
{
}

static void
parent_state_callback(rpl_parent_t *parent, int known, int edc)
{
}

static rpl_rank_t
calculate_rank(rpl_parent_t *p, rpl_rank_t base_rank)
{
  if(base_rank == 0) {
    if(p == NULL) {
      return INFINITE_RANK;
    }
    base_rank = p->rank;
  }
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

  instance->mc.type = RPL_DAG_MC_ETX;
  instance->mc.length = sizeof(instance->mc.obj.etx);
  instance->mc.obj.etx = e2e_edc;

  PRINTF("RPL: My path EDC to the root is %u (%u.%02u)\n",
      e2e_edc,
      e2e_edc / EDC_DIVISOR,
      (e2e_edc % EDC_DIVISOR * 100) / EDC_DIVISOR
  );
}

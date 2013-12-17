#include "contiki.h"
#include "net/uip.h"
#include "bloom.h"
#include "node-id.h"
#include <string.h>
#include <stdio.h>

#if OMNISCIENT_BLOOM
static uint16_t curr_hash;

static void
init_hash(unsigned char *ptr, int size) {
  curr_hash = node_id_from_ipaddr((const uip_ipaddr_t *)ptr);
}

/* Must be called after init_hash. Returns the next hash. */
static uint16_t
get_next_hash() {
  return curr_hash;
}

#else

/*** Hash implementation
 * We use a SAX (shift-and-xor hash)
 * ***/

static uint64_t curr_hash;

/* Called before getting the k hash values. Argument is the value to be inserted */

static void
init_hash(unsigned char *ptr, int size) {
  int i;
//#if BLOOM_FILTER_K <= 4
//  curr_hash = 0;
//#else
  curr_hash = *((uint64_t*)(ptr+8));
//#endif
  for(i=0; i<size; i++) {
    curr_hash ^= ( curr_hash << 5 ) + ( curr_hash >> 2 ) + ptr[i];
  }
}

/* Must be called after init_hash. Returns the next hash. */
static uint16_t
get_next_hash() {
  uint16_t ret = curr_hash;
  curr_hash >>= BLOOM_HASH_SHIFT;
  return ret;
}

#endif

/* Bit manipulation */

static void
setbit(double_bf *dbf, int i) {
  /* Always set in both filters */
  dbf->filters[0][i/8] |= 1 << (i%8);
  dbf->filters[1][i/8] |= 1 << (i%8);
}

static int
getbit(double_bf *dbf, int i) {
  /* Get in the active filter */
  return (dbf->filters[dbf->current][i/8] & (1 << (i%8))) != 0;
}

/*** Bloom filter implementation. Works with any hash. ***/

/* Initializes a double bloom filter */
void
bloom_init(double_bf *dbf) {
  memset(dbf, 0, sizeof(double_bf));
}

/* Inserts an element in a double bloom filter */
void
bloom_insert(double_bf *dbf, unsigned char *ptr, int size) {
  int k;
//  printf("Bloom: inserting %u\n", node_id_from_ipaddr(ptr));
  init_hash(ptr, size);
  /* For each hash, set a bit in the double bloom filter */
  for(k=0; k<BLOOM_FILTER_K; k++) {
    setbit(dbf, get_next_hash() % BLOOM_FILTER_SIZE);
  }
  dbf->insert_count_current++;
  dbf->insert_count_warmup++;
}

/* Merges a bloom filter with our double bloom filter */
void
bloom_merge(double_bf *dbf, bloom_filter bf, uint16_t id) {
  int i;
//  printf("Bloom: merging filter from %u\n", id);
  for(i=0; i<sizeof(bloom_filter); i++) {
    dbf->filters[0][i] |= bf[i];
    dbf->filters[1][i] |= bf[i];
  }
}

/* Checks is a double bloom filter contains an element */
int
bloom_contains(double_bf *dbf, unsigned char *ptr, int size) {
  int k;
  int contains = 1;
  init_hash(ptr, size);
  /* For each hash, check a bit in the bloom filter */
  for(k=0; k<BLOOM_FILTER_K; k++) {
    if(getbit(dbf, get_next_hash() % BLOOM_FILTER_SIZE) == 0) {
      /* If one bucket is empty, then the element isn't included in the filter */
      contains = 0;
      break;
    }
  }
//  printf("bloom looking for: ");
//  uip_debug_ipaddr_print(ptr);
//  printf(" (%s)\n", contains?"ok":"ko");
  return contains;
}

void
bloom_swap(double_bf *dbf) {
//  printf("Bloom: swapping\n");
  /* Swap active and inactive */
  dbf->current = 1 - dbf->current;
  /* Reset the newly inactive filter */
  memset(dbf->filters[1 - dbf->current], 0, sizeof(bloom_filter));

  dbf->insert_count_current = dbf->insert_count_warmup;
  dbf->insert_count_warmup = 0;
}

int
bloom_count_bits(double_bf *dbf) {
  int i;
    int cnt = 0;
    for(i=0; i<BLOOM_FILTER_SIZE; i++) {
      if(getbit(dbf, i)) {
        cnt++;
      }
    }
  return cnt;
}

void
bloom_print(double_bf *dbf) {
  printf("BFdump: bits set %d/%d, inserts %d, %d\n", bloom_count_bits(dbf), BLOOM_FILTER_SIZE, dbf->insert_count_current, dbf->insert_count_warmup);
  printf("BFdump: start\n");
  int i;
  for(i=0; i<BLOOM_FILTER_SIZE/8; i++) {
    if(i%16 == 0) {
      printf("BFdump: [%2u] ", i/16);
    }
    printf("%02x ", dbf->filters[dbf->current][i]);
    if(i%16 == 15) {
      printf("\n");
    }
  }
  printf("\nBFdump: end\n");
}
//
//void bloom_test(double_bf *dbf) {
//  uip_ipaddr_t ipaddr;
//  int i;
//  watchdog_stop();
//  bloom_init(dbf);
//  printf("\nInserting\n");
//  for(i=0;i<80;i++) {
//    node_ip6addr(&ipaddr, get_id(i));
//    bloom_insert(dbf, &ipaddr, 16);
//    bloom_print(dbf);
//  }
//
//  printf("\nInserted %d elements\n", i);
//  bloom_print(dbf);
//
//  printf("\nLooking up\n");
//  for(i=0;i<90;i++) {
//      node_ip6addr(&ipaddr, get_id(i));
//      printf("Bloom: looking up %3u: %d\n", node_id_from_ipaddr(&ipaddr), bloom_contains(dbf, &ipaddr, 16));
//    }
//
//  watchdog_start();
//
//}

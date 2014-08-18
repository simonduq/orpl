/*
 * Copyright (c) 2013, Swedish Institute of Computer Science.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
  *
 */
/**
 * \file
 *         Tools for logging ORPL state and tracing data packets
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki.h"
#include "orpl.h"
#include "orpl-routing-set.h"
#include "deployment.h"
#include "tools/simple-energest.h"
#include "net/rpl/rpl.h"
#include "net/rpl/rpl-private.h"
#include "net/packetbuf.h"
#include "tools/orpl-log.h"
#include <stdio.h>
#include <string.h>

extern int forwarder_set_size;
static int neighbor_set_size = 0;

/* Copy an appdata to another with no assumption that the addresses are aligned */
void
appdata_copy(struct app_data *dst, struct app_data *src)
{
  int i;
  for(i=0; i<sizeof(struct app_data); i++) {
    ((char*)dst)[i] = (((char*)src)[i]);
  }
}

/* Get dataptr from the packet currently in uIP buffer */
struct app_data *
appdataptr_from_uip()
{
  struct app_data *ptr;
  struct app_data data;
  if(uip_len < sizeof(struct app_data)) return NULL;
  ptr = (struct app_data *)((char*)uip_buf + ((uip_len - sizeof(struct app_data))));
  appdata_copy(&data, ptr);
  if(data.magic == ORPL_LOG_MAGIC) {
    return ptr;
  } else {
    return NULL;
  }
}

/* Get dataptr from the current packetbuf */
struct app_data *
appdataptr_from_packetbuf()
{
  struct app_data *ptr;
  struct app_data data;
  if(packetbuf_datalen() < sizeof(struct app_data)) return NULL;
  ptr = (struct app_data *)((char*)packetbuf_dataptr() + ((packetbuf_datalen() - sizeof(struct app_data))));
  appdata_copy(&data, ptr);
  if(data.magic == ORPL_LOG_MAGIC) {
    return ptr;
  } else {
    return NULL;
  }
}

/* Log information about a data packet along with ORPL routing information */
void
log_appdataptr(struct app_data *dataptr)
{
  struct app_data data;
  int curr_dio_interval = default_instance != NULL ? default_instance->dio_intcurrent : 0;
  int curr_rank = default_instance != NULL ? default_instance->current_dag->rank : 0xffff;

  if(dataptr) {
    appdata_copy(&data, dataptr);

    ORPL_LOG(" [%lx %u_%u %u->%u]",
        data.seqno,
        data.hop,
        data.fpcount,
        data.src,
        data.dest
        );
  }

  ORPL_LOG(" {%u/%u %u %u} \n",
#if WITH_ORPL
        forwarder_set_size,
#else
        1,
#endif /* WITH_ORPL */
        neighbor_set_size,
        curr_rank,
        curr_dio_interval
        );
}

/* Return node id from its rime address */
uint16_t
log_node_id_from_rimeaddr(const void *rimeaddr)
{
  return node_id_from_rimeaddr((const rimeaddr_t *)rimeaddr);
}

/* Return node id from its IP address */
uint16_t
log_node_id_from_ipaddr(const void *ipaddr)
{
  return node_id_from_ipaddr((const uip_ipaddr_t *)ipaddr);
}

/* Print all neighbors (RPL "parents"), their link metric and rank */
static void
rpl_print_neighbor_list() {
  rpl_parent_t *p = nbr_table_head(rpl_parents);
  ORPL_LOG("RPL: neighbor list\n");
  while(p != NULL) {
    ORPL_LOG("RPL: nbr %d %u + %u = %u %c\n",
        node_id_from_rimeaddr(nbr_table_get_lladdr(rpl_parents, p)), p->rank, p->link_metric, p->rank + p->link_metric, p==default_instance->current_dag->preferred_parent?'*':' ');
    p = nbr_table_next(rpl_parents, p);
  }
  ORPL_LOG("RPL: end of neighbor list\n");
}

/* Print all neighbors, their rank and other relevant information */
void
orpl_log_print_neighbor_list()
{
#if WITH_ORPL
  orpl_calculate_edc(1);
#else
  rpl_print_neighbor_list();
#endif
}

/* Prints out the content of the active routing set */
void
orpl_log_print_routing_set()
{
  ORPL_LOG("Routing set dump: bits set %d/%d\n", orpl_routing_set_count_bits(), ROUTING_SET_M);
  ORPL_LOG("Routing set dump: start\n");
  int i;
  for(i=0; i<ROUTING_SET_M/8; i++) {
    if(i%16 == 0) {
      ORPL_LOG("Routing set dump: [%2u] ", i/16);
    }
    ORPL_LOG("%02x ", orpl_routing_set_get_active()->u8[i]);
    if(i%16 == 15) {
      ORPL_LOG("\n");
    }
  }
  ORPL_LOG("\nRouting set dump: end\n");

  int count = 0;
  int print_header = 1;
  ORPL_LOG("Routing set list: start\n");
  for(i=0; i<get_n_nodes(); i++) {
    if(print_header) {
      ORPL_LOG("Routing set list: [%2u]", count/8);
      print_header = 0;
    }
    uip_ipaddr_t dest_ipaddr;
    uint16_t id = get_node_id_from_index(i);
    set_ipaddr_from_id(&dest_ipaddr, id);
    int contained = orpl_routing_set_contains(&dest_ipaddr);
    if(contained) {
      count+=1;
      ORPL_LOG("%3u, ", id);
      if(count%8 == 0) {
        ORPL_LOG("\n");
        print_header = 1;
      }
    }
  }
  ORPL_LOG("\nRouting set list: end (%u nodes)\n",count);
}

PROCESS(orpl_log_process, "ORPL Log");

/* Starts logging process */
void
orpl_log_start() {
  process_start(&orpl_log_process, NULL);
}

/* The logging process */
PROCESS_THREAD(orpl_log_process, ev, data)
{
  static struct etimer periodic;
  PROCESS_BEGIN();
  etimer_set(&periodic, 60 * CLOCK_SECOND);
  simple_energest_start();

  while(1) {
    static int cnt = 0;
    neighbor_set_size = uip_ds6_nbr_num();

    PROCESS_WAIT_UNTIL(etimer_expired(&periodic));
    etimer_reset(&periodic);
    simple_energest_step();

    /* Periodic debugging of neighbors and ranks */
    ORPL_LOG_PRINT_NEIGHBOR_LIST();

#if WITH_ORPL
    /* Periodic debugging of ORPL routing sets */
    if(orpl_are_routing_set_active() && ++cnt % 8 == 0) {
      orpl_log_print_routing_set();
    }
#endif /* WITH_ORPL */

  }

  PROCESS_END();
}

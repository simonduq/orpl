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
 *         Code managing id<->mac address<->IPv6 address mapping, and doing this
 *         for different deployment scenarios: Cooja, Nodes, Indriya or Twist testbeds
 *
 * \author Simon Duquennoy <simonduq@sics.se>
 */

#include "contiki-conf.h"
#include "deployment.h"
#include "sys/node-id.h"
#include "net/rpl/rpl.h"
#include "random.h"
#include "ds2411.h"
#include "orpl.h"
#include <string.h>

/* Our global IPv6 prefix */
static uip_ipaddr_t prefix;

/* ID<->MAC address mapping */
struct id_mac {
  uint16_t id;
  rimeaddr_t mac;
};

/* List of ID<->MAC mapping used for different deployments */
static const struct id_mac id_mac_list[] = {
#if IN_INDRIYA
    // TODO
#endif
    {0, {{0, 0, 0, 0, 0, 0, 0, 0}}}
};

/* The total number of nodes in the deployment */
#if IN_COOJA
#define N_NODES 8
#else
#define N_NODES ((sizeof(id_mac_list)/sizeof(struct id_mac))-1)
#endif

/* Returns the node's node-id */
uint16_t
get_node_id()
{
  return node_id_from_rimeaddr((const rimeaddr_t *)&ds2411_id);
}

/* Returns the total number of nodes in the deployment */
uint16_t
get_n_nodes()
{
  return N_NODES;
}

/* Returns a node-id from a node's rimeaddr */
uint16_t
node_id_from_rimeaddr(const rimeaddr_t *addr)
{
#if IN_COOJA
  if(addr == NULL) return 0;
  else return addr->u8[7];
#else /* IN_COOJA */
  if(addr == NULL) {
    return 0;
  }
  const struct id_mac *curr = id_mac_list;
  while(curr->id != 0) {
    if(rimeaddr_cmp(&curr->mac, addr)) {
      return curr->id;
    }
    curr++;
  }
  return 0;
#endif /* IN_COOJA */
}

/* Returns a node-id from a node's IPv6 address */
uint16_t
node_id_from_ipaddr(const uip_ipaddr_t *addr)
{
  uip_lladdr_t lladdr;
  lladdr_from_ipaddr_uuid(&lladdr, addr);
  return node_id_from_rimeaddr((const rimeaddr_t *)&lladdr);
}

/* Returns a node-id from a node's absolute index in the deployment */
uint16_t
get_node_id_from_index(uint16_t index)
{
#if IN_COOJA
  return 1 + (index % N_NODES);
#else
  return id_mac_list[index % N_NODES].id;
#endif
}

/* Sets an IPv6 from a node-id */
void
set_ipaddr_from_id(uip_ipaddr_t *ipaddr, uint16_t id)
{
  rimeaddr_t lladdr;
  memcpy(ipaddr, &prefix, 8);
  set_rimeaddr_from_id(&lladdr, id);
  uip_ds6_set_addr_iid(ipaddr, (uip_lladdr_t*)&lladdr);
}

/* Sets an rimeaddr from a link-layer address */
/* Sets a rimeaddr from a node-id */
void
set_rimeaddr_from_id(rimeaddr_t *lladdr, uint16_t id)
{
#if 0 && IN_COOJA
  lladdr->u8[0] = 0x00;
  lladdr->u8[1] = 0x12;
  lladdr->u8[2] = 0x74;
  lladdr->u8[3] = id;
  lladdr->u8[4] = 0x00;
  lladdr->u8[5] = id;
  lladdr->u8[6] = id;
  lladdr->u8[7] = id;
#else
  if(id == 0 || lladdr == NULL) {
    return;
  }
  const struct id_mac *curr = id_mac_list;
  while(curr->id != 0) {
    if(curr->id == id) {
      rimeaddr_copy(lladdr, &curr->mac);
      return;
    }
    curr++;
  }
#endif
}

/* Initializes global IPv6 and creates DODAG */
void
deployment_init(uip_ipaddr_t *ipaddr) {
  uint16_t id = get_node_id();
  rpl_dag_t *dag;

  uip_ip6addr(&prefix, 0xaaaa, 0, 0, 0, 0, 0, 0, 0);
  set_ipaddr_from_id(ipaddr, id);
  uip_ds6_addr_add(ipaddr, 0, ADDR_AUTOCONF);

  if(node_id == ROOT_ID) {
    rpl_set_root(RPL_DEFAULT_INSTANCE, ipaddr);
    dag = rpl_get_any_dag();
    rpl_set_prefix(dag, &prefix, 64);
  }
}

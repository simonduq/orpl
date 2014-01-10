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
 * This file is part of the Contiki operating system.
 *
 */

/**
 * \file
 *         orpl.c header file
 * \author
 *         Simon Duquennoy <simonduq@sics.se>
 */

#ifndef __ORPL_H__
#define __ORPL_H__

#include "net/rpl/rpl.h"
#include "net/netstack.h"
#include "net/mac/frame802154.h"
#include "net/uip-ds6.h"
#include "orpl-log.h"
#include "orpl-routing-set.h"

#define EDC_DIVISOR 128
#define EDC_TICKS_TO_METRIC(edc) (uint16_t)((edc) / (CONTIKIMAC_CONF_CYCLE_TIME / EDC_DIVISOR))

#define EXTRA_ACK_LEN 10

/* The IPv6 prefix in use */
uip_ipaddr_t prefix;

#define DO_ACK            1 /* Set if a link-layer ack must be sent */
#define IS_ANYCAST        2 /* Set if the packet is a anycast */
#define FROM_SUBDODAG     4 /* Set if the packet is coming from the sub-dodag (going upwards) */
#define IS_RECOVERY       8 /* Set if the packet comes from false positive recovery */

extern int forwarder_set_size;
extern int neighbor_set_size;
extern uint32_t anycast_count_incomming;
extern uint32_t anycast_count_acked;
extern uint32_t orpl_routing_set_merged_count;
extern int sending_routing_set;
extern uint32_t orpl_broadcast_count;

void orpl_print_ranks();
int is_reachable_neighbor(const uip_ipaddr_t *ipaddr);
void orpl_trickle_callback(rpl_instance_t *instance);
void broadcast_acked(const rimeaddr_t *receiver);
void broadcast_done();
void orpl_blacklist_insert(uint32_t seqno);
int blacklist_contains(uint32_t seqno);
void acked_down_insert(uint32_t seqno, uint16_t id);
int acked_down_contains(uint32_t seqno, uint16_t id);
void orpl_init(const uip_ipaddr_t *my_ipaddr, int is_root, int up_only);
int orpl_is_edc_frozen();
void update_annotations();
rpl_rank_t orpl_current_edc();
void orpl_update_edc(rpl_rank_t edc);
int orpl_is_root();
void routing_set_packet_sent(void *ptr, int status, int transmissions);

#endif /* __ORPL_H__ */

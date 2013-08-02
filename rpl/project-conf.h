/*
 * Copyright (c) 2010, Swedish Institute of Computer Science.
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
 * $Id: project-conf.h,v 1.1 2010/10/21 18:23:44 joxe Exp $
 */

#ifndef __PROJECT_CONF_H__
#define __PROJECT_CONF_H__

#include "common-conf.h"

#define UP_ONLY 1
#define ALL_NODES_ADDRESSABLE 0

#undef RPL_CONF_MOP
#if UP_ONLY
#define RPL_CONF_MOP RPL_MOP_NO_DOWNWARD_ROUTES
#else
#define RPL_CONF_MOP RPL_MOP_STORING_NO_MULTICAST
#endif

/* RPL and neighborhood information */

#define RPL_CONF_INIT_LINK_METRIC 6 /* default 5 */

/* Reject parents that have a higher link metric than the following. */
#define MAX_LINK_METRIC     10

#undef NEIGHBOR_CONF_MAX_NEIGHBORS
#undef UIP_CONF_DS6_NBR_NBU
#undef RPL_CONF_MAX_PARENTS_PER_DAG

#define NEIGHBOR_CONF_MAX_NEIGHBORS 16
#define UIP_CONF_DS6_NBR_NBU  16
#define RPL_CONF_MAX_PARENTS_PER_DAG 16

#undef UIP_CONF_DS6_ROUTE_NBU
#define UIP_CONF_DS6_ROUTE_NBU  70

/* Other system parameters */

#undef WITH_PHASE_OPTIMIZATION
#define WITH_PHASE_OPTIMIZATION CMD_PHASE_LOCK

#undef CC2420_CONF_SFD_TIMESTAMPS
#define CC2420_CONF_SFD_TIMESTAMPS 0

#undef SICSLOWPAN_CONF_MAX_MAC_TRANSMISSIONS
#define SICSLOWPAN_CONF_MAX_MAC_TRANSMISSIONS   5

#define MIN_DIO_RECEIVED 3

#endif /* __PROJECT_CONF_H__ */

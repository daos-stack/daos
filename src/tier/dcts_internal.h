/**
 * (C) Copyright 2016 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * dsms: Internal Declarations
 *
 * This file contains all declarations that are only used by dcts but do not
 * belong to the more specific headers.  All external
 * variables and functions must have a "dcts_" prefix, however, even if they
 * are only used by dsms.
 **/

#ifndef __DCTS_INTERNAL_H__
#define __DCTS_INTERNAL_H__

/* dcts_ping.c */

/* ping test handler, more of a self-teaching widget */
int dcts_hdlr_ping(dtp_rpc_t *rpc);

int dcts_hdlr_fetch(dtp_rpc_t *rpc);

#endif /*__DCTS_INTERNAL_H__*/

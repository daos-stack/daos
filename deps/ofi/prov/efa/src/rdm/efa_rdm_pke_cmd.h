/* SPDX-License-Identifier: BSD-2-Clause OR GPL-2.0-only */
/* SPDX-FileCopyrightText: Copyright Amazon.com, Inc. or its affiliates. All rights reserved. */

#ifndef _efa_rdm_pke_CMD_H
#define _efa_rdm_pke_CMD_H

#include <rdma/fi_endpoint.h>
#include <stdint.h>
#include "efa_rdm_pkt_type.h"

int efa_rdm_pke_fill_data(struct efa_rdm_pke *pke,
			  int pkt_type,
			  struct efa_rdm_ope *ope,
			  int64_t data_offset,
			  int data_size);

void efa_rdm_pke_handle_sent(struct efa_rdm_pke *pke, int pkt_type);

fi_addr_t efa_rdm_pke_determine_addr(struct efa_rdm_pke *pkt_entry);

void efa_rdm_pke_handle_data_copied(struct efa_rdm_pke *pkt_entry);

void efa_rdm_pke_handle_tx_error(struct efa_rdm_pke *pkt_entry, int prov_errno);

void efa_rdm_pke_handle_send_completion(struct efa_rdm_pke *pkt_entry);

void efa_rdm_pke_handle_rx_error(struct efa_rdm_pke *pkt_entry, int prov_errno);

void efa_rdm_pke_handle_recv_completion(struct efa_rdm_pke *pkt_entry);

void efa_rdm_pke_proc_received(struct efa_rdm_pke *pkt_entry);

void efa_rdm_pke_proc_received_no_hdr(struct efa_rdm_pke *pkt_entry, bool has_imm_data, uint32_t imm_data);

#if ENABLE_DEBUG
void efa_rdm_pke_print(struct efa_rdm_pke *pkt_entry, char *prefix);
#endif

#endif


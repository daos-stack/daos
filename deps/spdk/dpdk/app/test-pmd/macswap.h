/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Intel Corporation
 */

#ifndef _MACSWAP_H_
#define _MACSWAP_H_

#include "macswap_common.h"

static inline void
do_macswap(struct rte_mbuf *pkts[], uint16_t nb,
		struct rte_port *txp)
{
	struct rte_ether_hdr *eth_hdr;
	struct rte_mbuf *mb;
	struct rte_ether_addr addr;
	uint64_t ol_flags;
	int i;

	ol_flags = ol_flags_init(txp->dev_conf.txmode.offloads);
	vlan_qinq_set(pkts, nb, ol_flags,
			txp->tx_vlan_id, txp->tx_vlan_id_outer);

	for (i = 0; i < nb; i++) {
		if (likely(i < nb - 1))
			rte_prefetch0(rte_pktmbuf_mtod(pkts[i+1], void *));
		mb = pkts[i];

		eth_hdr = rte_pktmbuf_mtod(mb, struct rte_ether_hdr *);

		/* Swap dest and src mac addresses. */
		rte_ether_addr_copy(&eth_hdr->dst_addr, &addr);
		rte_ether_addr_copy(&eth_hdr->src_addr, &eth_hdr->dst_addr);
		rte_ether_addr_copy(&addr, &eth_hdr->src_addr);

		mbuf_field_set(mb, ol_flags);
	}
}

#endif /* _MACSWAP_H_ */

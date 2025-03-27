/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2018 Intel Corporation
 */

#ifndef _MACSWAP_COMMON_H_
#define _MACSWAP_COMMON_H_

static inline uint64_t
ol_flags_init(uint64_t tx_offload)
{
	uint64_t ol_flags = 0;

	ol_flags |= (tx_offload & RTE_ETH_TX_OFFLOAD_VLAN_INSERT) ?
			RTE_MBUF_F_TX_VLAN : 0;
	ol_flags |= (tx_offload & RTE_ETH_TX_OFFLOAD_QINQ_INSERT) ?
			RTE_MBUF_F_TX_QINQ : 0;
	ol_flags |= (tx_offload & RTE_ETH_TX_OFFLOAD_MACSEC_INSERT) ?
			RTE_MBUF_F_TX_MACSEC : 0;

	return ol_flags;
}

static inline void
vlan_qinq_set(struct rte_mbuf *pkts[], uint16_t nb,
		uint64_t ol_flags, uint16_t vlan, uint16_t outer_vlan)
{
	int i;

	if (ol_flags & RTE_MBUF_F_TX_VLAN)
		for (i = 0; i < nb; i++)
			pkts[i]->vlan_tci = vlan;
	if (ol_flags & RTE_MBUF_F_TX_QINQ)
		for (i = 0; i < nb; i++)
			pkts[i]->vlan_tci_outer = outer_vlan;
}

static inline void
mbuf_field_set(struct rte_mbuf *mb, uint64_t ol_flags)
{
	mb->ol_flags &= RTE_MBUF_F_INDIRECT | RTE_MBUF_F_EXTERNAL;
	mb->ol_flags |= ol_flags;
	mb->l2_len = sizeof(struct rte_ether_hdr);
	mb->l3_len = sizeof(struct rte_ipv4_hdr);
}

#endif /* _MACSWAP_COMMON_H_ */

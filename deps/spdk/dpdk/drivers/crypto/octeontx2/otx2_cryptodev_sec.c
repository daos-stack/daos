/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (C) 2020 Marvell International Ltd.
 */

#include <rte_cryptodev.h>
#include <rte_esp.h>
#include <rte_ethdev.h>
#include <rte_ip.h>
#include <rte_malloc.h>
#include <rte_security.h>
#include <rte_security_driver.h>
#include <rte_udp.h>

#include "otx2_cryptodev.h"
#include "otx2_cryptodev_capabilities.h"
#include "otx2_cryptodev_hw_access.h"
#include "otx2_cryptodev_ops.h"
#include "otx2_cryptodev_sec.h"
#include "otx2_security.h"

static int
ipsec_lp_len_precalc(struct rte_security_ipsec_xform *ipsec,
		struct rte_crypto_sym_xform *xform,
		struct otx2_sec_session_ipsec_lp *lp)
{
	struct rte_crypto_sym_xform *cipher_xform, *auth_xform;

	lp->partial_len = 0;
	if (ipsec->mode == RTE_SECURITY_IPSEC_SA_MODE_TUNNEL) {
		if (ipsec->tunnel.type == RTE_SECURITY_IPSEC_TUNNEL_IPV4)
			lp->partial_len = sizeof(struct rte_ipv4_hdr);
		else if (ipsec->tunnel.type == RTE_SECURITY_IPSEC_TUNNEL_IPV6)
			lp->partial_len = sizeof(struct rte_ipv6_hdr);
		else
			return -EINVAL;
	}

	if (ipsec->proto == RTE_SECURITY_IPSEC_SA_PROTO_ESP) {
		lp->partial_len += sizeof(struct rte_esp_hdr);
		lp->roundup_len = sizeof(struct rte_esp_tail);
	} else if (ipsec->proto == RTE_SECURITY_IPSEC_SA_PROTO_AH) {
		lp->partial_len += OTX2_SEC_AH_HDR_LEN;
	} else {
		return -EINVAL;
	}

	if (ipsec->options.udp_encap)
		lp->partial_len += sizeof(struct rte_udp_hdr);

	if (xform->type == RTE_CRYPTO_SYM_XFORM_AEAD) {
		if (xform->aead.algo == RTE_CRYPTO_AEAD_AES_GCM) {
			lp->partial_len += OTX2_SEC_AES_GCM_IV_LEN;
			lp->partial_len += OTX2_SEC_AES_GCM_MAC_LEN;
			lp->roundup_byte = OTX2_SEC_AES_GCM_ROUNDUP_BYTE_LEN;
			return 0;
		} else {
			return -EINVAL;
		}
	}

	if (ipsec->direction == RTE_SECURITY_IPSEC_SA_DIR_EGRESS) {
		cipher_xform = xform;
		auth_xform = xform->next;
	} else if (ipsec->direction == RTE_SECURITY_IPSEC_SA_DIR_INGRESS) {
		auth_xform = xform;
		cipher_xform = xform->next;
	} else {
		return -EINVAL;
	}

	if (cipher_xform->cipher.algo == RTE_CRYPTO_CIPHER_AES_CBC) {
		lp->partial_len += OTX2_SEC_AES_CBC_IV_LEN;
		lp->roundup_byte = OTX2_SEC_AES_CBC_ROUNDUP_BYTE_LEN;
	} else {
		return -EINVAL;
	}

	if (auth_xform->auth.algo == RTE_CRYPTO_AUTH_SHA1_HMAC)
		lp->partial_len += OTX2_SEC_SHA1_HMAC_LEN;
	else if (auth_xform->auth.algo == RTE_CRYPTO_AUTH_SHA256_HMAC)
		lp->partial_len += OTX2_SEC_SHA2_HMAC_LEN;
	else
		return -EINVAL;

	return 0;
}

static int
otx2_cpt_enq_sa_write(struct otx2_sec_session_ipsec_lp *lp,
		      struct otx2_cpt_qp *qptr, uint8_t opcode)
{
	uint64_t lmt_status, time_out;
	void *lmtline = qptr->lmtline;
	struct otx2_cpt_inst_s inst;
	struct otx2_cpt_res *res;
	uint64_t *mdata;
	int ret = 0;

	if (unlikely(rte_mempool_get(qptr->meta_info.pool,
				     (void **)&mdata) < 0))
		return -ENOMEM;

	res = (struct otx2_cpt_res *)RTE_PTR_ALIGN(mdata, 16);
	res->compcode = CPT_9X_COMP_E_NOTDONE;

	inst.opcode = opcode | (lp->ctx_len << 8);
	inst.param1 = 0;
	inst.param2 = 0;
	inst.dlen = lp->ctx_len << 3;
	inst.dptr = rte_mempool_virt2iova(lp);
	inst.rptr = 0;
	inst.cptr = rte_mempool_virt2iova(lp);
	inst.egrp  = OTX2_CPT_EGRP_SE;

	inst.u64[0] = 0;
	inst.u64[2] = 0;
	inst.u64[3] = 0;
	inst.res_addr = rte_mempool_virt2iova(res);

	rte_io_wmb();

	do {
		/* Copy CPT command to LMTLINE */
		otx2_lmt_mov(lmtline, &inst, 2);
		lmt_status = otx2_lmt_submit(qptr->lf_nq_reg);
	} while (lmt_status == 0);

	time_out = rte_get_timer_cycles() +
			DEFAULT_COMMAND_TIMEOUT * rte_get_timer_hz();

	while (res->compcode == CPT_9X_COMP_E_NOTDONE) {
		if (rte_get_timer_cycles() > time_out) {
			rte_mempool_put(qptr->meta_info.pool, mdata);
			otx2_err("Request timed out");
			return -ETIMEDOUT;
		}
	    rte_io_rmb();
	}

	if (unlikely(res->compcode != CPT_9X_COMP_E_GOOD)) {
		ret = res->compcode;
		switch (ret) {
		case CPT_9X_COMP_E_INSTERR:
			otx2_err("Request failed with instruction error");
			break;
		case CPT_9X_COMP_E_FAULT:
			otx2_err("Request failed with DMA fault");
			break;
		case CPT_9X_COMP_E_HWERR:
			otx2_err("Request failed with hardware error");
			break;
		default:
			otx2_err("Request failed with unknown hardware "
				 "completion code : 0x%x", ret);
		}
		goto mempool_put;
	}

	if (unlikely(res->uc_compcode != OTX2_IPSEC_PO_CC_SUCCESS)) {
		ret = res->uc_compcode;
		switch (ret) {
		case OTX2_IPSEC_PO_CC_AUTH_UNSUPPORTED:
			otx2_err("Invalid auth type");
			break;
		case OTX2_IPSEC_PO_CC_ENCRYPT_UNSUPPORTED:
			otx2_err("Invalid encrypt type");
			break;
		default:
			otx2_err("Request failed with unknown microcode "
				 "completion code : 0x%x", ret);
		}
	}

mempool_put:
	rte_mempool_put(qptr->meta_info.pool, mdata);
	return ret;
}

static void
set_session_misc_attributes(struct otx2_sec_session_ipsec_lp *sess,
			    struct rte_crypto_sym_xform *crypto_xform,
			    struct rte_crypto_sym_xform *auth_xform,
			    struct rte_crypto_sym_xform *cipher_xform)
{
	if (crypto_xform->type == RTE_CRYPTO_SYM_XFORM_AEAD) {
		sess->iv_offset = crypto_xform->aead.iv.offset;
		sess->iv_length = crypto_xform->aead.iv.length;
		sess->aad_length = crypto_xform->aead.aad_length;
		sess->mac_len = crypto_xform->aead.digest_length;
	} else {
		sess->iv_offset = cipher_xform->cipher.iv.offset;
		sess->iv_length = cipher_xform->cipher.iv.length;
		sess->auth_iv_offset = auth_xform->auth.iv.offset;
		sess->auth_iv_length = auth_xform->auth.iv.length;
		sess->mac_len = auth_xform->auth.digest_length;
	}
}

static int
crypto_sec_ipsec_outb_session_create(struct rte_cryptodev *crypto_dev,
				     struct rte_security_ipsec_xform *ipsec,
				     struct rte_crypto_sym_xform *crypto_xform,
				     struct rte_security_session *sec_sess)
{
	struct rte_crypto_sym_xform *auth_xform, *cipher_xform;
	struct otx2_ipsec_po_ip_template *template = NULL;
	const uint8_t *cipher_key, *auth_key;
	struct otx2_sec_session_ipsec_lp *lp;
	struct otx2_ipsec_po_sa_ctl *ctl;
	int cipher_key_len, auth_key_len;
	struct otx2_ipsec_po_out_sa *sa;
	struct otx2_sec_session *sess;
	struct otx2_cpt_inst_s inst;
	struct rte_ipv6_hdr *ip6;
	struct rte_ipv4_hdr *ip;
	int ret, ctx_len;

	sess = get_sec_session_private_data(sec_sess);
	sess->ipsec.dir = RTE_SECURITY_IPSEC_SA_DIR_EGRESS;
	lp = &sess->ipsec.lp;

	sa = &lp->out_sa;
	ctl = &sa->ctl;
	if (ctl->valid) {
		otx2_err("SA already registered");
		return -EINVAL;
	}

	memset(sa, 0, sizeof(struct otx2_ipsec_po_out_sa));

	/* Initialize lookaside ipsec private data */
	lp->ip_id = 0;
	lp->seq_lo = 1;
	lp->seq_hi = 0;

	ret = ipsec_po_sa_ctl_set(ipsec, crypto_xform, ctl);
	if (ret)
		return ret;

	ret = ipsec_lp_len_precalc(ipsec, crypto_xform, lp);
	if (ret)
		return ret;

	/* Start ip id from 1 */
	lp->ip_id = 1;

	if (ctl->enc_type == OTX2_IPSEC_PO_SA_ENC_AES_GCM) {
		template = &sa->aes_gcm.template;
		ctx_len = offsetof(struct otx2_ipsec_po_out_sa,
				aes_gcm.template) + sizeof(
				sa->aes_gcm.template.ip4);
		ctx_len = RTE_ALIGN_CEIL(ctx_len, 8);
		lp->ctx_len = ctx_len >> 3;
	} else if (ctl->auth_type ==
			OTX2_IPSEC_PO_SA_AUTH_SHA1) {
		template = &sa->sha1.template;
		ctx_len = offsetof(struct otx2_ipsec_po_out_sa,
				sha1.template) + sizeof(
				sa->sha1.template.ip4);
		ctx_len = RTE_ALIGN_CEIL(ctx_len, 8);
		lp->ctx_len = ctx_len >> 3;
	} else if (ctl->auth_type ==
			OTX2_IPSEC_PO_SA_AUTH_SHA2_256) {
		template = &sa->sha2.template;
		ctx_len = offsetof(struct otx2_ipsec_po_out_sa,
				sha2.template) + sizeof(
				sa->sha2.template.ip4);
		ctx_len = RTE_ALIGN_CEIL(ctx_len, 8);
		lp->ctx_len = ctx_len >> 3;
	} else {
		return -EINVAL;
	}
	ip = &template->ip4.ipv4_hdr;
	if (ipsec->options.udp_encap) {
		ip->next_proto_id = IPPROTO_UDP;
		template->ip4.udp_src = rte_be_to_cpu_16(4500);
		template->ip4.udp_dst = rte_be_to_cpu_16(4500);
	} else {
		ip->next_proto_id = IPPROTO_ESP;
	}

	if (ipsec->mode == RTE_SECURITY_IPSEC_SA_MODE_TUNNEL) {
		if (ipsec->tunnel.type == RTE_SECURITY_IPSEC_TUNNEL_IPV4) {
			ip->version_ihl = RTE_IPV4_VHL_DEF;
			ip->time_to_live = ipsec->tunnel.ipv4.ttl;
			ip->type_of_service |= (ipsec->tunnel.ipv4.dscp << 2);
			if (ipsec->tunnel.ipv4.df)
				ip->fragment_offset = BIT(14);
			memcpy(&ip->src_addr, &ipsec->tunnel.ipv4.src_ip,
				sizeof(struct in_addr));
			memcpy(&ip->dst_addr, &ipsec->tunnel.ipv4.dst_ip,
				sizeof(struct in_addr));
		} else if (ipsec->tunnel.type ==
				RTE_SECURITY_IPSEC_TUNNEL_IPV6) {

			if (ctl->enc_type == OTX2_IPSEC_PO_SA_ENC_AES_GCM) {
				template = &sa->aes_gcm.template;
				ctx_len = offsetof(struct otx2_ipsec_po_out_sa,
						aes_gcm.template) + sizeof(
						sa->aes_gcm.template.ip6);
				ctx_len = RTE_ALIGN_CEIL(ctx_len, 8);
				lp->ctx_len = ctx_len >> 3;
			} else if (ctl->auth_type ==
					OTX2_IPSEC_PO_SA_AUTH_SHA1) {
				template = &sa->sha1.template;
				ctx_len = offsetof(struct otx2_ipsec_po_out_sa,
						sha1.template) + sizeof(
						sa->sha1.template.ip6);
				ctx_len = RTE_ALIGN_CEIL(ctx_len, 8);
				lp->ctx_len = ctx_len >> 3;
			} else if (ctl->auth_type ==
					OTX2_IPSEC_PO_SA_AUTH_SHA2_256) {
				template = &sa->sha2.template;
				ctx_len = offsetof(struct otx2_ipsec_po_out_sa,
						sha2.template) + sizeof(
						sa->sha2.template.ip6);
				ctx_len = RTE_ALIGN_CEIL(ctx_len, 8);
				lp->ctx_len = ctx_len >> 3;
			} else {
				return -EINVAL;
			}

			ip6 = &template->ip6.ipv6_hdr;
			if (ipsec->options.udp_encap) {
				ip6->proto = IPPROTO_UDP;
				template->ip6.udp_src = rte_be_to_cpu_16(4500);
				template->ip6.udp_dst = rte_be_to_cpu_16(4500);
			} else {
				ip6->proto = (ipsec->proto ==
					RTE_SECURITY_IPSEC_SA_PROTO_ESP) ?
					IPPROTO_ESP : IPPROTO_AH;
			}
			ip6->vtc_flow = rte_cpu_to_be_32(0x60000000 |
				((ipsec->tunnel.ipv6.dscp <<
					RTE_IPV6_HDR_TC_SHIFT) &
					RTE_IPV6_HDR_TC_MASK) |
				((ipsec->tunnel.ipv6.flabel <<
					RTE_IPV6_HDR_FL_SHIFT) &
					RTE_IPV6_HDR_FL_MASK));
			ip6->hop_limits = ipsec->tunnel.ipv6.hlimit;
			memcpy(&ip6->src_addr, &ipsec->tunnel.ipv6.src_addr,
				sizeof(struct in6_addr));
			memcpy(&ip6->dst_addr, &ipsec->tunnel.ipv6.dst_addr,
				sizeof(struct in6_addr));
		}
	}

	cipher_xform = crypto_xform;
	auth_xform = crypto_xform->next;

	cipher_key_len = 0;
	auth_key_len = 0;

	if (crypto_xform->type == RTE_CRYPTO_SYM_XFORM_AEAD) {
		if (crypto_xform->aead.algo == RTE_CRYPTO_AEAD_AES_GCM)
			memcpy(sa->iv.gcm.nonce, &ipsec->salt, 4);
		cipher_key = crypto_xform->aead.key.data;
		cipher_key_len = crypto_xform->aead.key.length;
	} else {
		cipher_key = cipher_xform->cipher.key.data;
		cipher_key_len = cipher_xform->cipher.key.length;
		auth_key = auth_xform->auth.key.data;
		auth_key_len = auth_xform->auth.key.length;

		if (auth_xform->auth.algo == RTE_CRYPTO_AUTH_SHA1_HMAC)
			memcpy(sa->sha1.hmac_key, auth_key, auth_key_len);
		else if (auth_xform->auth.algo == RTE_CRYPTO_AUTH_SHA256_HMAC)
			memcpy(sa->sha2.hmac_key, auth_key, auth_key_len);
	}

	if (cipher_key_len != 0)
		memcpy(sa->cipher_key, cipher_key, cipher_key_len);
	else
		return -EINVAL;

	inst.u64[7] = 0;
	inst.egrp = OTX2_CPT_EGRP_SE;
	inst.cptr = rte_mempool_virt2iova(sa);

	lp->cpt_inst_w7 = inst.u64[7];
	lp->ucmd_opcode = (lp->ctx_len << 8) |
				(OTX2_IPSEC_PO_PROCESS_IPSEC_OUTB);

	/* Set per packet IV and IKEv2 bits */
	lp->ucmd_param1 = BIT(11) | BIT(9);
	lp->ucmd_param2 = 0;

	set_session_misc_attributes(lp, crypto_xform,
				    auth_xform, cipher_xform);

	return otx2_cpt_enq_sa_write(lp, crypto_dev->data->queue_pairs[0],
				     OTX2_IPSEC_PO_WRITE_IPSEC_OUTB);
}

static int
crypto_sec_ipsec_inb_session_create(struct rte_cryptodev *crypto_dev,
				    struct rte_security_ipsec_xform *ipsec,
				    struct rte_crypto_sym_xform *crypto_xform,
				    struct rte_security_session *sec_sess)
{
	struct rte_crypto_sym_xform *auth_xform, *cipher_xform;
	const uint8_t *cipher_key, *auth_key;
	struct otx2_sec_session_ipsec_lp *lp;
	struct otx2_ipsec_po_sa_ctl *ctl;
	int cipher_key_len, auth_key_len;
	struct otx2_ipsec_po_in_sa *sa;
	struct otx2_sec_session *sess;
	struct otx2_cpt_inst_s inst;
	int ret;

	sess = get_sec_session_private_data(sec_sess);
	sess->ipsec.dir = RTE_SECURITY_IPSEC_SA_DIR_INGRESS;
	lp = &sess->ipsec.lp;

	sa = &lp->in_sa;
	ctl = &sa->ctl;

	if (ctl->valid) {
		otx2_err("SA already registered");
		return -EINVAL;
	}

	memset(sa, 0, sizeof(struct otx2_ipsec_po_in_sa));
	sa->replay_win_sz = ipsec->replay_win_sz;

	ret = ipsec_po_sa_ctl_set(ipsec, crypto_xform, ctl);
	if (ret)
		return ret;

	auth_xform = crypto_xform;
	cipher_xform = crypto_xform->next;

	cipher_key_len = 0;
	auth_key_len = 0;

	if (crypto_xform->type == RTE_CRYPTO_SYM_XFORM_AEAD) {
		if (crypto_xform->aead.algo == RTE_CRYPTO_AEAD_AES_GCM)
			memcpy(sa->iv.gcm.nonce, &ipsec->salt, 4);
		cipher_key = crypto_xform->aead.key.data;
		cipher_key_len = crypto_xform->aead.key.length;

		lp->ctx_len = offsetof(struct otx2_ipsec_po_in_sa,
					    aes_gcm.hmac_key[0]) >> 3;
		RTE_ASSERT(lp->ctx_len == OTX2_IPSEC_PO_AES_GCM_INB_CTX_LEN);
	} else {
		cipher_key = cipher_xform->cipher.key.data;
		cipher_key_len = cipher_xform->cipher.key.length;
		auth_key = auth_xform->auth.key.data;
		auth_key_len = auth_xform->auth.key.length;

		if (auth_xform->auth.algo == RTE_CRYPTO_AUTH_SHA1_HMAC) {
			memcpy(sa->aes_gcm.hmac_key, auth_key, auth_key_len);
			lp->ctx_len = offsetof(struct otx2_ipsec_po_in_sa,
						    aes_gcm.selector) >> 3;
		} else if (auth_xform->auth.algo ==
				RTE_CRYPTO_AUTH_SHA256_HMAC) {
			memcpy(sa->sha2.hmac_key, auth_key, auth_key_len);
			lp->ctx_len = offsetof(struct otx2_ipsec_po_in_sa,
						    sha2.selector) >> 3;
		}
	}

	if (cipher_key_len != 0)
		memcpy(sa->cipher_key, cipher_key, cipher_key_len);
	else
		return -EINVAL;

	inst.u64[7] = 0;
	inst.egrp = OTX2_CPT_EGRP_SE;
	inst.cptr = rte_mempool_virt2iova(sa);

	lp->cpt_inst_w7 = inst.u64[7];
	lp->ucmd_opcode = (lp->ctx_len << 8) |
				(OTX2_IPSEC_PO_PROCESS_IPSEC_INB);
	lp->ucmd_param1 = 0;

	/* Set IKEv2 bit */
	lp->ucmd_param2 = BIT(12);

	set_session_misc_attributes(lp, crypto_xform,
				    auth_xform, cipher_xform);

	if (sa->replay_win_sz) {
		if (sa->replay_win_sz > OTX2_IPSEC_MAX_REPLAY_WIN_SZ) {
			otx2_err("Replay window size is not supported");
			return -ENOTSUP;
		}
		sa->replay = rte_zmalloc(NULL, sizeof(struct otx2_ipsec_replay),
				0);
		if (sa->replay == NULL)
			return -ENOMEM;

		/* Set window bottom to 1, base and top to size of window */
		sa->replay->winb = 1;
		sa->replay->wint = sa->replay_win_sz;
		sa->replay->base = sa->replay_win_sz;
		sa->esn_low = 0;
		sa->esn_hi = 0;
	}

	return otx2_cpt_enq_sa_write(lp, crypto_dev->data->queue_pairs[0],
				     OTX2_IPSEC_PO_WRITE_IPSEC_INB);
}

static int
crypto_sec_ipsec_session_create(struct rte_cryptodev *crypto_dev,
				struct rte_security_ipsec_xform *ipsec,
				struct rte_crypto_sym_xform *crypto_xform,
				struct rte_security_session *sess)
{
	int ret;

	if (crypto_dev->data->queue_pairs[0] == NULL) {
		otx2_err("Setup cpt queue pair before creating sec session");
		return -EPERM;
	}

	ret = ipsec_po_xform_verify(ipsec, crypto_xform);
	if (ret)
		return ret;

	if (ipsec->direction == RTE_SECURITY_IPSEC_SA_DIR_INGRESS)
		return crypto_sec_ipsec_inb_session_create(crypto_dev, ipsec,
							   crypto_xform, sess);
	else
		return crypto_sec_ipsec_outb_session_create(crypto_dev, ipsec,
							    crypto_xform, sess);
}

static int
otx2_crypto_sec_session_create(void *device,
			       struct rte_security_session_conf *conf,
			       struct rte_security_session *sess,
			       struct rte_mempool *mempool)
{
	struct otx2_sec_session *priv;
	int ret;

	if (conf->action_type != RTE_SECURITY_ACTION_TYPE_LOOKASIDE_PROTOCOL)
		return -ENOTSUP;

	if (rte_security_dynfield_register() < 0)
		return -rte_errno;

	if (rte_mempool_get(mempool, (void **)&priv)) {
		otx2_err("Could not allocate security session private data");
		return -ENOMEM;
	}

	set_sec_session_private_data(sess, priv);

	priv->userdata = conf->userdata;

	if (conf->protocol == RTE_SECURITY_PROTOCOL_IPSEC)
		ret = crypto_sec_ipsec_session_create(device, &conf->ipsec,
						      conf->crypto_xform,
						      sess);
	else
		ret = -ENOTSUP;

	if (ret)
		goto mempool_put;

	return 0;

mempool_put:
	rte_mempool_put(mempool, priv);
	set_sec_session_private_data(sess, NULL);
	return ret;
}

static int
otx2_crypto_sec_session_destroy(void *device __rte_unused,
				struct rte_security_session *sess)
{
	struct otx2_sec_session *priv;
	struct rte_mempool *sess_mp;

	priv = get_sec_session_private_data(sess);

	if (priv == NULL)
		return 0;

	sess_mp = rte_mempool_from_obj(priv);

	memset(priv, 0, sizeof(*priv));

	set_sec_session_private_data(sess, NULL);
	rte_mempool_put(sess_mp, priv);

	return 0;
}

static unsigned int
otx2_crypto_sec_session_get_size(void *device __rte_unused)
{
	return sizeof(struct otx2_sec_session);
}

static int
otx2_crypto_sec_set_pkt_mdata(void *device __rte_unused,
			      struct rte_security_session *session,
			      struct rte_mbuf *m, void *params __rte_unused)
{
	/* Set security session as the pkt metadata */
	*rte_security_dynfield(m) = (rte_security_dynfield_t)session;

	return 0;
}

static int
otx2_crypto_sec_get_userdata(void *device __rte_unused, uint64_t md,
			     void **userdata)
{
	/* Retrieve userdata  */
	*userdata = (void *)md;

	return 0;
}

static struct rte_security_ops otx2_crypto_sec_ops = {
	.session_create		= otx2_crypto_sec_session_create,
	.session_destroy	= otx2_crypto_sec_session_destroy,
	.session_get_size	= otx2_crypto_sec_session_get_size,
	.set_pkt_metadata	= otx2_crypto_sec_set_pkt_mdata,
	.get_userdata		= otx2_crypto_sec_get_userdata,
	.capabilities_get	= otx2_crypto_sec_capabilities_get
};

int
otx2_crypto_sec_ctx_create(struct rte_cryptodev *cdev)
{
	struct rte_security_ctx *ctx;

	ctx = rte_malloc("otx2_cpt_dev_sec_ctx",
			 sizeof(struct rte_security_ctx), 0);

	if (ctx == NULL)
		return -ENOMEM;

	/* Populate ctx */
	ctx->device = cdev;
	ctx->ops = &otx2_crypto_sec_ops;
	ctx->sess_cnt = 0;

	cdev->security_ctx = ctx;

	return 0;
}

void
otx2_crypto_sec_ctx_destroy(struct rte_cryptodev *cdev)
{
	rte_free(cdev->security_ctx);
}

/**
 * (C) Copyright 2025 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
#define D_LOGFAC DD_FAC(tree)

#include <daos_errno.h>
#include <daos/btree.h>

#include <daos_srv/btree_check.h>

#include "btree_internal.h"

#define DLCK_BTREE_NODE_MALFORMED_STR "malformed - "
#define DLCK_BTREE_NON_ZERO_PADDING_FMT                                                            \
	DLCK_BTREE_NODE_MALFORMED_STR "non-zero padding (%#" PRIx32 ")"
#define DLCK_BTREE_NON_ZERO_GEN_FMT DLCK_BTREE_NODE_MALFORMED_STR "nd_gen != 0 (%#" PRIx32 ")"

/**
 * Validate the integrity of the btree node.
 *
 * \param[in] nd	Node to check.
 * \param[in] nd_off	Node's offset.
 * \param[in] dp	DLCK print utility.
 *
 * \retval DER_SUCCESS	The node is correct.
 * \retval -DER_NOTYPE	The node is malformed.
 */
int
btr_node_check(struct btr_node *nd, umem_off_t nd_off, struct checker *ck)
{
	uint16_t unknown_flags;

	D_ASSERT(ck != NULL);
	CK_PRINTF(ck, "Node (off=%#x)... ", nd_off);

	unknown_flags = nd->tn_flags & ~(BTR_NODE_LEAF | BTR_NODE_ROOT);
	if (unknown_flags != 0) {
		CK_APPENDFL_ERR(ck, DLCK_BTREE_NODE_MALFORMED_STR "unknown flags (%#" PRIx16 ")",
				unknown_flags);
		return -DER_NOTYPE;
	}

	if (nd->tn_pad_32 != 0) {
		if (ck->ck_options.cko_non_zero_padding == CHECKER_EVENT_ERROR) {
			CK_APPENDFL_ERR(ck, DLCK_BTREE_NON_ZERO_PADDING_FMT, nd->tn_pad_32);
			return -DER_NOTYPE;
		} else {
			CK_APPENDFL_WARN(ck, DLCK_BTREE_NON_ZERO_PADDING_FMT, nd->tn_pad_32);
		}
	}

	if (nd->tn_gen != 0) {
		if (ck->ck_options.cko_non_zero_padding == CHECKER_EVENT_ERROR) {
			CK_APPENDFL_ERR(ck, DLCK_BTREE_NON_ZERO_GEN_FMT, nd->tn_gen);
			return -DER_NOTYPE;
		} else {
			CK_APPENDFL_WARN(ck, DLCK_BTREE_NON_ZERO_GEN_FMT, nd->tn_gen);
		}
	}

	CK_APPENDL_OK(ck);

	return DER_SUCCESS;
}

/**
 * Validate the integrity of a btree.
 *
 * \param[in]	toh	Tree handle.
 * \param[in]	ck	Checker.
 *
 * \retval DER_SUCCESS		The tree is correct.
 * \retval -DER_NOTYPE		The tree is malformed.
 * \retval -DER_NONEXIST	The tree is malformed.
 * \retval -DER_*		Possibly other errors.
 */
static int
dlck_dbtree_check(daos_handle_t toh, struct checker *ck)
{
	daos_handle_t ih;
	int           rc;

	D_ASSERT(ck != NULL);

	rc = dbtree_iter_prepare(toh, BTR_ITER_EMBEDDED, &ih);
	if (rc != 0) {
		CK_PRINTL_RC(ck, rc, "failed to prepare tree iterator");
		return rc;
	}

	rc = dbtree_iter_probe(ih, BTR_PROBE_FIRST, DAOS_INTENT_CHECK, NULL /** key */,
			       NULL /** anchor */);
	if (rc == -DER_NONEXIST) {
		rc = DER_SUCCESS;
		goto err_iter_finish;
	}
	if (rc != DER_SUCCESS) {
		CK_PRINTL_RC(ck, rc, "failed to initialize tree iterator");
	}

	while (rc == DER_SUCCESS) {
		rc = dbtree_iter_next(ih);
		if (rc == -DER_NONEXIST) {
			rc = 0;
			break;
		} else if (rc != DER_SUCCESS) {
			CK_PRINTL_RC(ck, rc, "failed to move tree iterator");
			break;
		}
	}

err_iter_finish:
	(void)dbtree_iter_finish(ih);

	return rc;
}

/**
 * Open a btree from the root address.
 *
 * \param[in] root	Address of the tree root.
 * \param[in] uma	Memory class attributes.
 * \param[in] coh	The container open handle.
 * \param[in] priv	Private data for tree opener
 * \param[in] ck	Checker.
 * \param[out] toh	Returned tree open handle.
 */
int
dbtree_open_inplace_ck(struct btr_root *root, struct umem_attr *uma, daos_handle_t coh, void *priv,
		       struct checker *ck, daos_handle_t *toh)
{
	int rc = dbtree_open_inplace_ex_internal(root, uma, coh, priv, ck, toh);
	if (rc != DER_SUCCESS) {
		return rc;
	}

	/** This check is conducted only for the checker's purpose. No need to do it otherwise. */
	if (IS_CHECKER(ck)) {
		CK_PRINT(ck, "Nodes:\n");
		CK_INDENT(ck, rc = dlck_dbtree_check(*toh, ck));
		if (rc != DER_SUCCESS) {
			dbtree_close(*toh);
		}
	}

	return rc;
}

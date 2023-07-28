/*
 * (C) Copyright 2016-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of CaRT. It gives out the generic tree topo related
 * function implementation.
 */
#define D_LOGFAC	DD_FAC(grp)

#include "crt_internal.h"

static int
crt_get_filtered_grp_rank_list(struct crt_grp_priv *grp_priv, uint32_t grp_ver,
			       bool filter_invert, d_rank_list_t *filter_ranks,
			       d_rank_t root, d_rank_t self, d_rank_t *grp_size,
			       uint32_t *grp_root, d_rank_t *grp_self,
			       d_rank_list_t **result_grp_rank_list,
			       bool *allocated)
{
	d_rank_list_t		*grp_rank_list = NULL;
	d_rank_list_t		*membs;
	int			 rc = 0;

	membs = grp_priv_get_membs(grp_priv);

	rc = d_rank_list_dup_sort_uniq(&grp_rank_list, membs);
	if (rc != 0) {
		D_ERROR("d_rank_list_dup failed, rc " DF_RC "\n", DP_RC(rc));
		D_GOTO(out, rc);
	}
	D_ASSERT(grp_rank_list != NULL);
	*allocated = true;

	if (filter_invert) {
		d_rank_list_filter(filter_ranks, grp_rank_list,
				   false /* exclude */);
		if (grp_rank_list->rl_nr != filter_ranks->rl_nr) {
			D_ERROR("%u/%u filter ranks (inverted) out of group\n",
				filter_ranks->rl_nr - grp_rank_list->rl_nr,
				filter_ranks->rl_nr);
			d_rank_list_free(grp_rank_list);
			grp_rank_list = NULL;
			D_GOTO(out, rc = -DER_OOG);
		}
	} else if (filter_ranks != NULL && filter_ranks->rl_nr > 0) {
		d_rank_list_filter(filter_ranks, grp_rank_list,
				   true /* exclude */);
		if (grp_rank_list->rl_nr == 0) {
			D_DEBUG(DB_TRACE, "d_rank_list_filter(group %s) "
				"get empty.\n", grp_priv->gp_pub.cg_grpid);
			d_rank_list_free(grp_rank_list);
			grp_rank_list = NULL;
			D_GOTO(out, rc = 0);
		}
	}
	*grp_size = grp_rank_list->rl_nr;

	rc = d_idx_in_rank_list(grp_rank_list, root, grp_root);
	if (rc != 0) {
		D_ERROR("d_idx_in_rank_list (group %s, rank %d), "
			"failed, rc: %d.\n", grp_priv->gp_pub.cg_grpid,
			root, rc);
		d_rank_list_free(grp_rank_list);
		D_GOTO(out, rc);
	}

	rc = d_idx_in_rank_list(grp_rank_list, self, grp_self);
	if (rc != 0) {
		D_ERROR("d_idx_in_rank_list (group %s, rank %d), "
			"failed, rc: %d.\n", grp_priv->gp_pub.cg_grpid,
			self, rc);
		d_rank_list_free(grp_rank_list);
		D_GOTO(out, rc);
	}

out:
	if (rc == 0)
		*result_grp_rank_list = grp_rank_list;
	return rc;
}


#define CRT_TREE_PARAMETER_CHECKING(grp_priv, tree_topo, root, self)	\
	do {								\
									\
		D_ASSERT(crt_tree_topo_valid(tree_topo));		\
		tree_type = crt_tree_type(tree_topo);			\
		tree_ratio = crt_tree_ratio(tree_topo);			\
		D_ASSERT(tree_type >= CRT_TREE_MIN &&			\
			 tree_type <= CRT_TREE_MAX);			\
		D_ASSERT(tree_type == CRT_TREE_FLAT ||			\
			 (tree_ratio >= CRT_TREE_MIN_RATIO &&		\
			  tree_ratio <= CRT_TREE_MAX_RATIO));		\
		D_ASSERT(root != CRT_NO_RANK);				\
		D_ASSERT(self != CRT_NO_RANK);				\
	} while (0)

/*
 * query number of children.
 *
 * rank number of grp_priv->gp_membs and exclude_ranks
 * are primary rank.  grp_root and grp_self are logical rank number within the
 * group.
 */
int
crt_tree_get_nchildren(struct crt_grp_priv *grp_priv, uint32_t grp_ver,
		       d_rank_list_t *exclude_ranks, int tree_topo,
		       d_rank_t root, d_rank_t self, uint32_t *nchildren)
{
	d_rank_list_t		*grp_rank_list = NULL;
	d_rank_t		 grp_root, grp_self;
	bool			 allocated = false;
	uint32_t		 tree_type, tree_ratio;
	uint32_t		 grp_size;
	struct crt_topo_ops	*tops;
	int			 rc = 0;

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);

	CRT_TREE_PARAMETER_CHECKING(grp_priv, tree_topo, root, self);
	if (nchildren == NULL) {
		D_ERROR("invalid parameter of NULL nchildren.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/*
	 * grp_rank_list is the target group (filtered out the excluded ranks)
	 * for building the tree, rank number in it is for primary group.
	 */
	rc = crt_get_filtered_grp_rank_list(grp_priv, grp_ver,
					    false /* filter_invert */,
					    exclude_ranks, root, self,
					    &grp_size, &grp_root, &grp_self,
					    &grp_rank_list, &allocated);
	if (rc != 0) {
		D_ERROR("crt_get_filtered_grp_rank_list(group %s, root %d, "
			"self %d) failed, rc: %d.\n", grp_priv->gp_pub.cg_grpid,
			root, self, rc);
		D_GOTO(out, rc);
	}
	if (grp_rank_list == NULL) {
		D_ERROR("crt_get_filtered_grp_rank_list(group %s) get empty.\n",
			grp_priv->gp_pub.cg_grpid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	tops = crt_tops[tree_type];
	rc = tops->to_get_children_cnt(grp_size, tree_ratio, grp_root, grp_self,
				       nchildren);
	if (rc != 0)
		D_ERROR("to_get_children_cnt (group %s, root %d, self %d) "
			"failed, rc: %d.\n", grp_priv->gp_pub.cg_grpid,
			root, self, rc);

out:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	if (allocated)
		d_rank_list_free(grp_rank_list);
	return rc;
}

/*
 * query children rank list (rank number in primary group).
 *
 * rank number of grp_priv->gp_membs, and filter_ranks
 * are primary rank.  grp_root and grp_self are logical rank number within the
 * group.
 */
int
crt_tree_get_children(struct crt_grp_priv *grp_priv, uint32_t grp_ver,
		      bool filter_invert, d_rank_list_t *filter_ranks,
		      int tree_topo, d_rank_t root, d_rank_t self,
		      d_rank_list_t **children_rank_list, bool *ver_match)
{
	d_rank_list_t		*grp_rank_list = NULL;
	d_rank_list_t		*result_rank_list = NULL;
	d_rank_t		 grp_root, grp_self;
	bool			 allocated = false;
	uint32_t		 tree_type, tree_ratio;
	uint32_t		 grp_size, nchildren;
	uint32_t		 *tree_children;
	struct crt_topo_ops	*tops;
	int			 i, rc = 0;


	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);
	if (ver_match != NULL) {
		*ver_match = (bool)(grp_ver == grp_priv->gp_membs_ver);

		if (*ver_match == false) {
			D_DEBUG(DB_ALL,
				"Version mismatch. Passed: %u current:%u\n",
				grp_ver, grp_priv->gp_membs_ver);
			D_GOTO(out, rc = -DER_GRPVER);
		}
	}

	CRT_TREE_PARAMETER_CHECKING(grp_priv, tree_topo, root, self);
	if (children_rank_list == NULL) {
		D_ERROR("invalid parameter of NULL children_rank_list.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/*
	 * grp_rank_list is the target group (after applying filter_ranks)
	 * for building the tree, rank number in it is for primary group.
	 */
	rc = crt_get_filtered_grp_rank_list(grp_priv, grp_ver, filter_invert,
					    filter_ranks, root, self, &grp_size,
					    &grp_root, &grp_self,
					    &grp_rank_list, &allocated);
	if (rc != 0) {
		D_ERROR("crt_get_filtered_grp_rank_list(group %s, root %d, "
			"self %d) failed, rc " DF_RC "\n",
			grp_priv->gp_pub.cg_grpid, root, self, DP_RC(rc));
		D_GOTO(out, rc);
	}

	if (grp_rank_list == NULL) {
		D_DEBUG(DB_TRACE, "crt_get_filtered_grp_rank_list(group %s) "
			"get empty.\n", grp_priv->gp_pub.cg_grpid);
		*children_rank_list = NULL;
		D_GOTO(out, rc);
	}

	tops = crt_tops[tree_type];

	rc = tops->to_get_children_cnt(grp_size, tree_ratio, grp_root, grp_self,
				       &nchildren);
	if (rc != 0) {
		D_ERROR("to_get_children_cnt (group %s, root %d, self %d) "
			"failed, rc: %d.\n", grp_priv->gp_pub.cg_grpid,
			root, self, rc);
		D_GOTO(out, rc);
	}

	if (nchildren == 0) {
		*children_rank_list = NULL;
		D_GOTO(out, rc = 0);
	}
	result_rank_list = d_rank_list_alloc(nchildren);
	if (result_rank_list == NULL)
		D_GOTO(out, rc = -DER_NOMEM);
	D_ALLOC_ARRAY(tree_children, nchildren);
	if (tree_children == NULL) {
		d_rank_list_free(result_rank_list);
		D_GOTO(out, rc = -DER_NOMEM);
	}
	rc = tops->to_get_children(grp_size, tree_ratio, grp_root, grp_self,
				   tree_children);
	if (rc != 0) {
		D_ERROR("to_get_children (group %s, root %d, self %d) "
			"failed, rc: %d.\n", grp_priv->gp_pub.cg_grpid,
			root, self, rc);
		d_rank_list_free(result_rank_list);
		D_FREE(tree_children);
		D_GOTO(out, rc);
	}

	for (i = 0; i < nchildren; i++)
		result_rank_list->rl_ranks[i] =
			grp_rank_list->rl_ranks[tree_children[i]];

	D_FREE(tree_children);
	*children_rank_list = result_rank_list;

out:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	if (allocated)
		d_rank_list_free(grp_rank_list);
	return rc;
}

int
crt_tree_get_parent(struct crt_grp_priv *grp_priv, uint32_t grp_ver,
		    d_rank_list_t *exclude_ranks, int tree_topo,
		    d_rank_t root, d_rank_t self, d_rank_t *parent_rank)
{
	d_rank_list_t		*grp_rank_list = NULL;
	d_rank_t		 grp_root, grp_self;
	bool			 allocated = false;
	uint32_t		 tree_type, tree_ratio;
	uint32_t		 grp_size, tree_parent;
	struct crt_topo_ops	*tops;
	int			 rc = 0;

	D_RWLOCK_RDLOCK(&grp_priv->gp_rwlock);

	CRT_TREE_PARAMETER_CHECKING(grp_priv, tree_topo, root, self);
	if (parent_rank == NULL) {
		D_ERROR("invalid parameter of NULL parent_rank.\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	/*
	 * grp_rank_list is the target group (filtered out the excluded ranks)
	 * for building the tree, rank number in it is for primary group.
	 */
	rc = crt_get_filtered_grp_rank_list(grp_priv, grp_ver,
					    false /* filter_invert */,
					    exclude_ranks, root, self,
					    &grp_size, &grp_root, &grp_self,
					    &grp_rank_list, &allocated);
	if (rc != 0) {
		D_ERROR("crt_get_filtered_grp_rank_list(group %s, root %d, "
			"self %d) failed, rc: %d.\n", grp_priv->gp_pub.cg_grpid,
			root, self, rc);
		D_GOTO(out, rc);
	}
	if (grp_rank_list == NULL) {
		D_DEBUG(DB_TRACE, "crt_get_filtered_grp_rank_list(group %s) "
			"get empty.\n", grp_priv->gp_pub.cg_grpid);
		D_GOTO(out, rc = -DER_INVAL);
	}

	tops = crt_tops[tree_type];
	rc = tops->to_get_parent(grp_size, tree_ratio, grp_root, grp_self,
				 &tree_parent);
	if (rc != 0) {
		D_ERROR("to_get_parent (group %s, root %d, self %d) failed, "
			"rc: %d.\n", grp_priv->gp_pub.cg_grpid, root, self, rc);
	}

	*parent_rank = grp_rank_list->rl_ranks[tree_parent];

out:
	D_RWLOCK_UNLOCK(&grp_priv->gp_rwlock);
	if (allocated)
		d_rank_list_free(grp_rank_list);
	return rc;
}

struct crt_topo_ops *crt_tops[] = {
	NULL,			/* CRT_TREE_INVALID */
	&crt_flat_ops,		/* CRT_TREE_FLAT */
	&crt_kary_ops,		/* CRT_TREE_KARY */
	&crt_knomial_ops,	/* CRT_TREE_KNOMIAL */
};

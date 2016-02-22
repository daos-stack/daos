/**
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Lesser General Public License
 * (LGPL) version 2.1 which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/lgpl-2.1.html
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the LGPL License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 *
 * (C) Copyright 2015 Intel Corporation.
 */
/**
 * This file is part of daos_sr
 *
 * dsr/tests/pseudo_cluster.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include <getopt.h>
#include <signal.h>
#include <math.h>
#include <pl_map.h>
#include <pseudo_cl_buf.h>

#define P_ARG_SEP	','
#define P_VAL_SEP	':'

#define PSC_PAUSE_MODE	"PAUSE"

/**
 * number of objects in a pseudo targets, buffer size can progressively
 * increase
 */
#define PSC_TARGET_SIZE		(8 * 1024)

typedef struct {
	pl_obj_shard_t		po_os;
	/** object metadata in shard */
	pl_obj_attr_t		po_attr;
} psc_obj_t;

/** pseudo target */
typedef struct {
	daos_rank_t		 pt_rank;
	/* buffer size of \a pt_objs */
	unsigned int		 pt_nobjs_max;
	/** # rebuilt/rebalanced object */
	unsigned int		 pt_nobjs_rb;
	/** # of object in this target */
	unsigned int		 pt_nobjs;
	/** objects in this target */
	psc_obj_t		*pt_objs;
	/** reference to the corresponding target in cluster map */
	cl_target_t		*pt_target;
} psc_target_t;

#define PSC_COMP_DESC_MAX	 8

typedef struct {
	char			*str;
	union {
		struct pa_cl_update {
			cl_pseudo_comp_desc_t	*c_descs;
			unsigned int		 c_ndescs;
			bool			 c_print;
		} cl_update;

		struct pa_pl_create {
			pl_map_type_t		 p_type;
			cl_comp_type_t		 p_domain;
			unsigned int		 p_num;
			bool			 p_print;
		} pl_create;

		struct pa_obj_create {
			unsigned int		 o_num;
			unsigned int		 o_rank;
			bool			 o_print;
			bool			 o_print_tgs;
		} obj_create;

		struct pa_target_change {
			unsigned int		 t_nops;
			struct {
				/* 0 is down, 1 is up */
				cl_comp_state_t	 state;
				daos_rank_t	 rank;
			} *t_ops;
			bool			 t_print;
		} target_change;
	} u;
} psc_argument_t;

typedef struct {
	/** cluster map */
	cl_map_t		*pg_clmap;
	/** rim placement map */
	pl_map_t		*pg_map;
	psc_target_t		*pg_targets;
	/** placement map arguments */
	struct pa_pl_create	 pg_pcr;
	uint64_t		 pg_oid_gen;
	unsigned long		 pg_nobjs_m;
	unsigned long		 pg_nobjs_sr;
	unsigned int		 pg_ntargets;
	unsigned int		 pg_ndescs;
	cl_pseudo_comp_desc_t	 pg_descs[PSC_COMP_DESC_MAX];
	pl_obj_attr_t		 pg_oa;
} psc_global_data_t;

/* global data */
static psc_global_data_t	pg_data;

static bool			wake_up;
static bool			pause_mode;

#define PSC_PROMPT(fmt, ...)						\
do {									\
	sigset_t  __ss;							\
									\
	fprintf(stdout, "\n");						\
	fprintf(stdout, "Command: "fmt, ## __VA_ARGS__);		\
	if (!pause_mode)						\
		break;							\
									\
	fprintf(stdout, "Press <Ctl^C> to execute\n");			\
	sigemptyset(&__ss);						\
	while (!wake_up)						\
		sigsuspend(&__ss);					\
	wake_up = false;						\
	fprintf(stdout, "\n");						\
} while (0)

static void psc_targets_print(unsigned int nranks, daos_rank_t *ranks);

static daos_obj_id_t
psc_oid_generate(void)
{
	daos_obj_id_t	id;

	id.body[0] = pg_data.pg_oid_gen++;
	id.body[1] = 0;
	return id;
}

enum {
	PPARSE_INVAL	= -1,
	PPARSE_OK	= 0,
	PPARSE_END	= 1,
};

static int
psc_target_cmp(void *array, int a, int b)
{
	psc_target_t	*pts = array;

	if (pts[a].pt_rank > pts[b].pt_rank)
		return 1;
	if (pts[a].pt_rank < pts[b].pt_rank)
		return -1;
	D_ASSERT(0);
	return 0;
}

static int
psc_target_cmp_key(void *array, int i, uint64_t key)
{
	psc_target_t	*pts = array;
	daos_rank_t	 rank = key;

	if (pts[i].pt_rank > rank)
		return 1;
	if (pts[i].pt_rank < rank)
		return -1;
	return 0;
}

static void
psc_target_swap(void *array, int a, int b)
{
	psc_target_t	*pts = array;
	psc_target_t	 tmp = pts[a];

	pts[a] = pts[b];
	pts[b] = tmp;
}

static daos_sort_ops_t psc_target_sort_ops = {
	.so_cmp		= psc_target_cmp,
	.so_cmp_key	= psc_target_cmp_key,
	.so_swap	= psc_target_swap,
};

int
psc_parse_next(char *str, char **endp)
{
	if (*str == ' ' || *str == '\t' || *str == '\0') {
		*endp = NULL;
		return PPARSE_END; /* end */
	}

	if (*str == P_ARG_SEP) { /* next parameter */
		*endp = str + 1;
		return PPARSE_OK;
	}

	return PPARSE_INVAL; /* invalid */
}

static int
psc_parse_number(char *str, unsigned int *rc_p, char **endp)
{
	unsigned int	val;

	if (*str++ != P_VAL_SEP)
		return PPARSE_INVAL;

	val = strtoul(str, &str, 0);
	*endp = str;
	*rc_p = val;
	return PPARSE_OK;
}

static char
psc_parse_char(char *str, char *rc_p, char **endp)
{
	char	c;

	if (*str++ != P_VAL_SEP)
		return PPARSE_INVAL;

	c = *str;
	*endp = str + 1;
	*rc_p = c;
	return PPARSE_OK;
}

static int
psc_targets_setup(unsigned int ntargets, cl_target_t *targets)
{
	psc_target_t	*old;
	psc_target_t	*pts;
	int		 old_ntgs;
	int		 i;

	old = pg_data.pg_targets;
	old_ntgs = pg_data.pg_ntargets;

	if (old_ntgs != 0) {
		D_PRINT("Cluster is extended from %d targets to %d\n",
			old_ntgs, ntargets);
	} else {
		D_PRINT("Cluster has %d targets\n", ntargets);
	}

	pg_data.pg_ntargets = ntargets;
	pts = calloc(ntargets, sizeof(*pg_data.pg_targets));
	if (pts == NULL)
		return -ENOMEM;

	pg_data.pg_targets = pts;

	for (i = 0; i < ntargets; i++) {
		int	pos = -1;

		pts[i].pt_rank = targets[i].co_rank;
		if (old != NULL) {
			pos = daos_array_find(old, old_ntgs, pts[i].pt_rank,
					      &psc_target_sort_ops);
			if (pos >= 0) {
				pts[i] = old[pos];
				pts[i].pt_target = &targets[i];
				continue;
			}
		}
		pts[i].pt_target    = &targets[i];
		pts[i].pt_nobjs_max = PSC_TARGET_SIZE;
		pts[i].pt_objs	    = calloc(PSC_TARGET_SIZE,
					     sizeof(*pts[i].pt_objs));
		if (pts[i].pt_objs == NULL)
			return -ENOMEM;

		if (i != 0 && i % 10000 == 0)
			D_PRINT("Created %d targets\n", i);
	}

	daos_array_sort(pts, ntargets, true, &psc_target_sort_ops);
	if (old != NULL)
		free(old);

	D_DEBUG(DF_CL, "Setup %d pseudo targets\n", ntargets);
	return 0;
}

static void
psc_targets_destroy(void)
{
	psc_target_t	*psts = pg_data.pg_targets;
	int		 i;

	if (psts == NULL)
		return;

	for (i = 0; i < pg_data.pg_ntargets; i++) {
		if (psts[i].pt_objs != NULL)
			free(psts[i].pt_objs);
	}

	pg_data.pg_targets = NULL;
	pg_data.pg_ntargets = 0;
	free(psts);
}

static psc_target_t *
psc_target_find(daos_rank_t rank)
{
	int	pos;

	pos = daos_array_find(pg_data.pg_targets, pg_data.pg_ntargets,
			      rank, &psc_target_sort_ops);

	return pos < 0 ? NULL : &pg_data.pg_targets[pos];
}

static void
psc_target_print(psc_target_t *pst, char *buf, int len, char **next)
{
	char		*tmp;
	const int	 slen = 18;
	int		 rc;

	if (pst == NULL) { /* last */
		D_PRINT("%s\n", buf);
		*next = buf;
		return;
	}

	tmp = *next;
	if ((len - (tmp - buf)) <= slen) {
		*tmp = 0;
		D_PRINT("%s\n", buf);
		*next = tmp = buf;
	}

	rc = sprintf(tmp, "%u[%c]: %u", pst->pt_target->co_rank,
		     pst->pt_target->co_status == CL_COMP_ST_UP ? ' ' : 'X',
		     pst->pt_nobjs);
	for (tmp += rc; rc < slen; rc++, tmp++)
		*tmp = ' ';

	*next = tmp;
}

static void
psc_targets_print(unsigned int nranks, daos_rank_t *ranks)
{
	psc_target_t	*pst;
	char		*tmp;
	char		 buf[80];
	int		 i;

	D_PRINT("Objects distribution in targets:\n");
	tmp = buf;
	if (ranks != NULL) {
		for (i = 0; i < nranks; i++) {
			pst = psc_target_find(ranks[i]);
			psc_target_print(pst, buf, 80, &tmp);
		}
	} else {
		pst = pg_data.pg_targets;

		for (i = 0; i < pg_data.pg_ntargets; i++, pst++)
			psc_target_print(pst, buf, 80, &tmp);
	}
	psc_target_print(NULL, buf, 80, &tmp);
}

static void
psc_obj_stats_print(void)
{
	psc_target_t	*pst;
	unsigned long	 obj_max = 0;
	unsigned long	 obj_min = -1;
	unsigned long	 range;
	unsigned long	 avg;
	int		 i;

	pst = pg_data.pg_targets;

	for (i = 0; i < pg_data.pg_ntargets; i++, pst++) {
		if (pst->pt_target->co_status != CL_COMP_ST_UP)
			continue;

		obj_max = MAX(pst->pt_nobjs, obj_max);
		obj_min = MIN(pst->pt_nobjs, obj_min);
	}

	range	= obj_max - obj_min;
	avg	= pg_data.pg_nobjs_m / pg_data.pg_ntargets;
	if (avg == 0 && pg_data.pg_nobjs_m != 0)
		avg = 1;

	D_PRINT("Total daos-sr objects %lu, daos-m objects %lu\n"
		"Best %lu, max %lu, min %lu, range %lu, percentage %-6.3f%%\n",
		pg_data.pg_nobjs_sr, pg_data.pg_nobjs_m, avg,
		obj_max, obj_min, range, (float)(range * 100) / avg);
}

static int
psc_target_append_obj(psc_target_t *pst, psc_obj_t *obj, bool rb)
{
	pl_obj_shard_t	*os = &obj->po_os;
	int		 nobjs = pst->pt_nobjs + pst->pt_nobjs_rb;

	obj->po_os.os_rank = pst->pt_rank;
	if (nobjs == pst->pt_nobjs_max) {
		psc_obj_t *objs;

		nobjs += PSC_TARGET_SIZE;
		objs = realloc(pst->pt_objs, nobjs * sizeof(*objs));
		if (objs == NULL)
			return -ENOSPC;

		pst->pt_objs = objs;
		pst->pt_nobjs_max += PSC_TARGET_SIZE;
	}

	nobjs = pst->pt_nobjs + pst->pt_nobjs_rb;
	if (rb) {
		D_DEBUG(DF_PL,
			"rebuild/rebalance obj "DF_U64".%u on target %u\n",
			os->os_id.body[0], os->os_sid, pst->pt_rank);

		pst->pt_objs[nobjs] = *obj;
		pst->pt_nobjs_rb++;
	} else {
		D_DEBUG(DF_PL, "Create obj "DF_U64".%u on target %u\n",
			os->os_id.body[0], os->os_sid, pst->pt_rank);
		if (pst->pt_rank == -1) {
			D_PRINT("Create obj "DF_U64".%u on target %u\n",
			os->os_id.body[0], os->os_sid, pst->pt_rank);
		}

		if (pst->pt_nobjs_rb != 0)
			pst->pt_objs[nobjs] = pst->pt_objs[pst->pt_nobjs];

		pst->pt_objs[pst->pt_nobjs] = *obj;
		pst->pt_nobjs++;
	}
	return 0;
}

static void
psc_target_del_obj(psc_target_t *pst, unsigned int index)
{
	int	nobjs = pst->pt_nobjs + pst->pt_nobjs_rb;

	if (index >= nobjs)
		return;

	if (index < pst->pt_nobjs) {
		pst->pt_nobjs--;
		pst->pt_objs[index] = pst->pt_objs[pst->pt_nobjs];
		if (pst->pt_nobjs_rb != 0)
			pst->pt_objs[pst->pt_nobjs] = pst->pt_objs[nobjs - 1];

	} else {
		D_ASSERT(pst->pt_nobjs_rb > 0);
		pst->pt_nobjs_rb--;
		pst->pt_objs[index] = pst->pt_objs[nobjs - 1];
	}
}

static int
psc_cl_parse_args(char *str, psc_argument_t *args)
{
	struct pa_cl_update	*clu = &args->u.cl_update;
	cl_pseudo_comp_desc_t	*desc;

	D_DEBUG(DF_CL, "parse parameters for cluster map: %s\n", str);

	/* always allocate a large buffer to simply things... */
	clu->c_descs  = calloc(PSC_COMP_DESC_MAX, sizeof(*desc));

	if (clu->c_descs == NULL)
		return -ENOMEM;

	for (desc = &clu->c_descs[0]; str != NULL;) {
		int	rc;

		switch (*str) {
		default:
			rc = psc_parse_next(str, &str);
			if (rc == PPARSE_OK || rc == PPARSE_END)
				break;

			D_DEBUG(DF_CL, "Invalid string: %s\n", str);
			return -EINVAL;

		case 't': case 'T':
		case 'n': case 'N':
		case 'b': case 'B':
		case 'l': case 'L':
		case 'r': case 'R':
			if (clu->c_ndescs >= PSC_COMP_DESC_MAX) {
				D_ERROR("Too many descriptors %d\n",
					clu->c_ndescs);
				return -EINVAL;
			}

			desc->cd_type = cl_comp_abbr2type(*str);
			if (desc->cd_type == CL_COMP_DUMMY) {
				D_DEBUG(DF_CL, "Unknown parameters %s\n", str);
				return -EINVAL;
			}

			rc = psc_parse_number(str + 1, &desc->cd_number, &str);
			if (rc == PPARSE_INVAL) {
				D_DEBUG(DF_CL, "can't parse number of %s\n",
					cl_comp_type2name(desc->cd_type));
				return -EINVAL;
			}

			if (*str == P_VAL_SEP) { /* more value */
				rc = psc_parse_number(str, &desc->cd_rank,
						      &str);
				if (rc == PPARSE_INVAL) {
					D_DEBUG(DF_CL, "Can't parse %s rank\n",
					      cl_comp_type2name(desc->cd_type));
					return -EINVAL;
				}
			} else {
				desc->cd_rank = -1;
			}
			clu->c_ndescs++;
			desc++;
			break;
		case 'p': /* print cluster map */
			clu->c_print = true;
			str++;
			break;
		}
	}

	if (desc - &clu->c_descs[0] <= 1) {
		D_ERROR("Please provide number of domains and targets\n");
		return -EINVAL;
	}
	return 0;
}

static int
psc_cl_create(psc_argument_t *args)
{
	struct pa_cl_update	*clu = &args->u.cl_update;
	cl_map_t		*map = NULL;
	cl_buf_t		*buf = NULL;
	int			 i;
	int			 rc;

	PSC_PROMPT("Create cluster map %s\n", args->str);

	for (i = 0; i < clu->c_ndescs; i++) {
		if (clu->c_descs[i].cd_rank == -1)
			clu->c_descs[i].cd_rank = 0;
	}

	rc = cl_pseudo_buf_build(clu->c_ndescs, clu->c_descs, true, &buf);
	if (rc != 0) {
		D_ERROR("Failed to create component buffer: %d\n", rc);
		goto out;
	}

	rc = cl_map_create(buf, &map);
	if (rc != 0) {
		D_ERROR("Failed to create cluster map: %d\n", rc);
		goto out;
	}

	if (clu->c_print)
		cl_map_print(map);

	pg_data.pg_ndescs = clu->c_ndescs;
	pg_data.pg_clmap = map;

	D_ASSERT(clu->c_ndescs <= PSC_COMP_DESC_MAX);
	memcpy(&pg_data.pg_descs[0], clu->c_descs,
	       clu->c_ndescs * sizeof(*clu->c_descs));
	map = NULL;
 out:
	if (buf != NULL)
		cl_pseudo_buf_free(buf);
	if (map != NULL)
		cl_map_destroy(map);
	free(clu->c_descs);
	return rc;
}

static int
psc_cl_change(psc_argument_t *args)
{
	cl_pseudo_comp_desc_t	*descs = &pg_data.pg_descs[0];
	struct pa_cl_update	*clu = &args->u.cl_update;
	cl_buf_t		*buf = NULL;
	bool			 root;
	int			 i;
	int			 j;
	int			 rc;

	PSC_PROMPT("Change cluster map %s\n", args->str);

	for (i = 0; i < pg_data.pg_ndescs; i++) {
		if (descs[i].cd_type == clu->c_descs[0].cd_type)
			break;
	}

	if (i == pg_data.pg_ndescs) {
		D_ERROR("Cannot find domain %s\n",
			cl_comp_type2name(clu->c_descs[0].cd_type));
		return -EINVAL;
	}

	root = i == 0;
	for (j = 0; j < clu->c_ndescs; j++, i++) {
		if (i >= pg_data.pg_ndescs ||
		    descs[i].cd_type != clu->c_descs[j].cd_type) {
			D_ERROR("Hierachy of New descriptor %d can't match "
				"with original descriptor, %s/%s\n", i,
				cl_comp_type2name(clu->c_descs[j].cd_type),
				cl_comp_type2name(descs[i].cd_type));
			return -EINVAL;
		}

		if (clu->c_descs[j].cd_rank == -1) {
			clu->c_descs[j].cd_rank = descs[i].cd_rank +
						  descs[i].cd_number;
		}

		if (clu->c_descs[j].cd_rank + clu->c_descs[j].cd_number >
		    descs[i].cd_rank + descs[i].cd_number) {
			descs[i].cd_number += (clu->c_descs[j].cd_rank +
					       clu->c_descs[j].cd_number) -
					      (descs[i].cd_rank +
					       descs[i].cd_number);
		}
	}

	rc = cl_pseudo_buf_build(clu->c_ndescs, clu->c_descs, root, &buf);
	if (rc != 0) {
		D_ERROR("Failed to create component buffer: %d\n", rc);
		goto out;
	}

	/* skip root */
	rc = cl_map_extend(pg_data.pg_clmap, buf);
	if (rc != 0) {
		D_ERROR("Failed to extend cluster map\n");
		goto out;
	}

	if (clu->c_print)
		cl_map_print(pg_data.pg_clmap);
 out:
	if (buf != NULL)
		cl_pseudo_buf_free(buf);
	return rc;
}

static int
psc_pl_parse_args(char *str, psc_argument_t *args)
{
	struct pa_pl_create *pcr = &args->u.pl_create;

	D_DEBUG(DF_PL, "parse parameters for placement map: %s\n", str);

	while (str != NULL) {
		int	rc;
		char	c;

		switch (*str) {
		default:
			rc = psc_parse_next(str, &str);
			if (rc == PPARSE_OK || rc == PPARSE_END)
				break;

			D_DEBUG(DF_CL, "Invalid string: %s\n", str);
			return -EINVAL;
		case 't': /* type of placement map */
			if (pcr->p_type != PL_TYPE_UNKNOWN) {
				D_ERROR("has set placement map type to %d\n",
					pcr->p_type);
				return -EINVAL;
			}

			rc = psc_parse_char(str + 1, &c, &str);
			if (rc == PPARSE_INVAL) {
				D_ERROR("Invalid string: %s\n", str);
				return -EINVAL;
			}

			if (c != 'r') { /* TODO: other placement map types */
				D_ERROR("can only support rim map: %c\n", c);
				return -EINVAL;
			}

			D_DEBUG(DF_PL, "placement map type: %d\n", pcr->p_type);
			pcr->p_type = PL_TYPE_RIM;
			break;
		case 'd': /* domain type */
			if (pcr->p_type != PL_TYPE_RIM)
				return -EINVAL;

			rc = psc_parse_char(str + 1, &c, &str);
			if (rc == PPARSE_INVAL)
				return -EINVAL;

			pcr->p_domain = cl_comp_abbr2type(c);
			if (pcr->p_domain == CL_COMP_DUMMY)
				return -EINVAL;
			break;
		case 'n': /* number of placement maps */
			rc = psc_parse_number(str + 1, &pcr->p_num, &str);
			if (rc == PPARSE_INVAL)
				return -EINVAL;
			break;
		case 'p': /* print cluster map */
			pcr->p_print = true;
			str++;
			break;
		}
	}

	if (pcr->p_type == PL_TYPE_UNKNOWN || pcr->p_num == 0) {
		D_ERROR("Please provide placement map type and number\n");
		return -EINVAL;
	}
	return 0;
}

static int
psc_pl_create(psc_argument_t *args)
{
	struct pa_pl_create	*pcr = &args->u.pl_create;
	pl_map_attr_t		 ma;
	int			 rc;

	if (pg_data.pg_clmap == NULL) {
		D_ERROR("should create cluster map first\n");
		return -EINVAL;
	}

	if (pg_data.pg_map == NULL) {
		PSC_PROMPT("Create placement maps %s\n", args->str);
	} else {
		pl_map_destroy(pg_data.pg_map);
		pg_data.pg_map = NULL;
	}

	D_DEBUG(DF_PL, "placement map %d, domain %s, num: %d\n",
		pcr->p_type, cl_comp_type2name(pcr->p_domain), pcr->p_num);

	switch (pcr->p_type) {
	default:
		D_ERROR("Unknown placement map type %d\n", pcr->p_type);
		return -EINVAL;
	case PL_TYPE_RIM:
		ma.ma_type = PL_TYPE_RIM;
		ma.ma_version = cl_map_version(pg_data.pg_clmap);
		ma.u.rim.ra_domain = pcr->p_domain;
		ma.u.rim.ra_nrims = pcr->p_num;

		rc = pl_map_create(pg_data.pg_clmap, &ma, &pg_data.pg_map);
		if (rc != 0)
			break;

		if (pcr->p_print)
			pl_map_print(pg_data.pg_map);
		break;
	}

	pg_data.pg_pcr = *pcr;
	return rc;
}

static int
psc_obj_schema_args(char *str, psc_argument_t *args)
{
	pl_obj_attr_t *oa = &pg_data.pg_oa;

	if (oa->oa_nstripes != 0 || oa->oa_rd_grp != 0 ||
	    oa->oa_nspares != 0) {
		D_ERROR("Can't set object distribution for multiple times\n");
		return -EINVAL;
	}

	D_DEBUG(DF_PL, "parse parameters for object distribution: %s\n", str);

	oa->oa_start	= -1; /* unsupported */
	oa->oa_nstripes = 1; /* default */
	oa->oa_rd_grp	= 3; /* default */
	oa->oa_nspares  = 1; /* default */

	while (str != NULL) {
		unsigned num;
		int	 rc;

		switch (*str) {
		default:
			rc = psc_parse_next(str, &str);
			if (rc == PPARSE_OK || rc == PPARSE_END)
				break;

			D_DEBUG(DF_CL, "Invalid string: %s\n", str);
			return -EINVAL;
		case 's':
			rc = psc_parse_number(str + 1, &num, &str);
			if (rc == PPARSE_INVAL)
				return -EINVAL;
			oa->oa_nstripes = num;
			break;
		case 'r':
			rc = psc_parse_number(str + 1, &num, &str);
			if (rc == PPARSE_INVAL)
				return -EINVAL;
			oa->oa_rd_grp = num;
			break;
		case 'k':
			rc = psc_parse_number(str + 1, &num, &str);
			if (rc == PPARSE_INVAL)
				return -EINVAL;
			oa->oa_spare_skip = num;
			break;
		case 'p':
			rc = psc_parse_number(str + 1, &num, &str);
			if (rc == PPARSE_INVAL)
				return -EINVAL;
			oa->oa_nspares = num;
			break;
		}
	}

	return 0;
}

static int
psc_obj_create_args(char *str, psc_argument_t *args)
{
	pl_obj_attr_t		*oa = &pg_data.pg_oa;
	struct pa_obj_create	*ocr = &args->u.obj_create;
	int			 rc;

	if (oa->oa_nstripes == 0 || oa->oa_rd_grp == 0) {
		D_ERROR("Please specify object distribution\n");
		return -EINVAL;
	}

	D_DEBUG(DF_PL, "parse parameters for object: %s\n", str);

	while (str != NULL) {
		switch (*str) {
		default:
			rc = psc_parse_next(str, &str);
			if (rc == PPARSE_OK || rc == PPARSE_END)
				break;

			D_DEBUG(DF_CL, "Invalid string: %s\n", str);
			return -EINVAL;
		case 'n':
			rc = psc_parse_number(str + 1, &ocr->o_num, &str);
			if (rc == PPARSE_INVAL)
				return -EINVAL;
			break;

		case 'p': /* print object layout */
			ocr->o_print = true;
			str++;
			break;
		case 'P': /* print object distribution */
			ocr->o_print_tgs = true;
			str++;
			break;
		}
	}
	return 0;
}

static void
psc_obj_print(daos_obj_id_t id, int nosas, pl_obj_shard_t *osas)
{
	pl_obj_attr_t	*oa = &pg_data.pg_oa;
	int		 i;
	int		 j;

	D_PRINT("OBJ["DF_U64"] : ", id.body[0]);
	for (i = 0; i < oa->oa_nstripes; i++) {
		D_PRINT("[");
		for (j = 0; j < oa->oa_rd_grp; j++) {
			D_PRINT("%d", osas[i * oa->oa_rd_grp + j].os_rank);
			if (j < oa->oa_rd_grp - 1)
				D_PRINT(" ");
		}
		D_PRINT("]");
		if (i < oa->oa_nstripes - 1)
			D_PRINT(" ");
	}
	D_PRINT("\n");
}

static int
osc_obj_create(psc_argument_t *args)
{
	pl_obj_attr_t		*oa = &pg_data.pg_oa;
	struct pa_obj_create	*ocr = &args->u.obj_create;
	pl_obj_shard_t		*osas = NULL;
	int			 nosas;
	int			 i;
	int			 j;
	int			 rc;

	PSC_PROMPT("Create objects %s, rd_grp %d, stripes %d, "
		   "spare %d, skip %d\n",
		   args->str, oa->oa_rd_grp, oa->oa_nstripes,
		   oa->oa_nspares, oa->oa_spare_skip);

	nosas = oa->oa_nstripes * oa->oa_rd_grp;
	osas = calloc(oa->oa_nstripes * oa->oa_rd_grp, sizeof(*osas));
	if (osas == NULL)
		return -ENOMEM;

	pg_data.pg_nobjs_sr += ocr->o_num;
	pg_data.pg_nobjs_m += ocr->o_num * (oa->oa_nstripes * oa->oa_rd_grp);

	for (i = 0; i < ocr->o_num; i++) {
		psc_obj_t	 obj;
		pl_obj_shard_t	*os = &obj.po_os;

		os->os_id = psc_oid_generate();
		os->os_sid = -1;
		os->os_rank = -1;
		os->os_stride = 0;
		rc = pl_map_obj_select(pg_data.pg_map, os, oa, PL_SEL_ALL,
				       nosas, osas);
		if (rc < 0)
			goto failed;

		rc = 0;
		for (j = 0; j < nosas; j++) {
			os->os_sid = osas[j].os_sid;
			os->os_rank = osas[j].os_rank;
			os->os_stride = osas[j].os_stride;
			obj.po_attr = *oa;
			psc_target_append_obj(psc_target_find(osas[j].os_rank),
					      &obj, false);
		}

		if (ocr->o_print)
			psc_obj_print(obj.po_os.os_id, nosas, osas);
		else if (i != 0 && i % 1000000 == 0)
			D_PRINT("created %d objects\n", i);
	}

	if (ocr->o_print_tgs)
		psc_targets_print(0, NULL);

	psc_obj_stats_print();
 failed:
	if (rc != 0)
		D_ERROR("Failed to create many objects\n");
	free(osas);
	return rc;
}

static int
psc_target_change_args(char *str, psc_argument_t *args)
{
	struct pa_target_change	*tgc = &args->u.target_change;
	char			*tmp;
	unsigned		 num;
	int			 i;

	D_DEBUG(DF_PL, "parse parameters for target change: %s\n", str);
	for (tmp = str, num = 0;(tmp = strchr(tmp, P_VAL_SEP)) != NULL;
	     tmp++, num++)
		;

	tgc->t_nops = num;
	tgc->t_ops  = calloc(num, sizeof(*tgc->t_ops));
	if (tgc->t_ops == NULL)
		return -ENOMEM;

	for (i = 0; str != NULL;) {
		int	c;
		int	rc;

		switch (*str) {
		default:
			rc = psc_parse_next(str, &str);
			if (rc == PPARSE_OK || rc == PPARSE_END)
				break;

			D_DEBUG(DF_CL, "Invalid string: %s\n", str);
			return -EINVAL;
		case 'e': /* eanble target */
		case 'd':
			c = *str;
			rc = psc_parse_number(str + 1, &num, &str);
			if (rc == PPARSE_INVAL)
				return -EINVAL;

			tgc->t_ops[i].rank = num;
			tgc->t_ops[i].state = c == 'd' ? CL_COMP_ST_DOWN :
							 CL_COMP_ST_UP;
			D_DEBUG(DF_CL, "%s target[%d]\n",
				c == 'd' ? "disable" : "enable", num);
			i++;
			break;
		case 'p': /* print object distribution */
			tgc->t_print = true;
			str++;
			break;
		}
	}
	return 0;
}

static void
psc_target_rebuild_objs(psc_target_t *pt, daos_rank_t failed_rank)
{
	cl_target_t	*target = pt->pt_target;
	int		 i;

	if (target->co_status != CL_COMP_ST_UP)
		return;

	D_DEBUG(DF_PL,
		"Check %d objects of target[%d] to rebuild failed target[%d]\n",
		pt->pt_nobjs, target->co_rank, failed_rank);

	for (i = 0; i < pt->pt_nobjs; i++) {
		psc_obj_t	 obj = pt->pt_objs[i];
		pl_obj_shard_t	 os_rbd;
		bool		 found;

		found = pl_map_obj_rebuild(pg_data.pg_map, &obj.po_os,
					   &obj.po_attr, failed_rank,
					   &os_rbd);
		if (found) {
			obj.po_os = os_rbd;
			psc_target_append_obj(psc_target_find(os_rbd.os_rank),
					      &obj, true);
		}
	}
}

static void
psc_target_rebuild(daos_rank_t failed_rank)
{
	psc_target_t	*pts	= pg_data.pg_targets;
	int		 nobjs	= 0;
	int		 ntgs	= 0;
	int		 i;

	for (i = 0; i < pg_data.pg_ntargets; i++)
		psc_target_rebuild_objs(&pts[i], failed_rank);

	/* clean all objects on the failed target */
	psc_target_find(failed_rank)->pt_nobjs = 0;

	for (i = 0; i < pg_data.pg_ntargets; i++) {
		if (pts[i].pt_nobjs_rb == 0)
			continue;

		D_PRINT("target[%d] took over %d objects from target[%d]\n",
			 pts[i].pt_target->co_rank, pts[i].pt_nobjs_rb,
			 failed_rank);

		nobjs += pts[i].pt_nobjs_rb;
		pts[i].pt_nobjs += pts[i].pt_nobjs_rb;
		pts[i].pt_nobjs_rb = 0;
		ntgs++;
	}

	if (nobjs != 0)
		D_PRINT("Rebuild %d objects on %d targets\n", nobjs, ntgs);
}

static int
psc_target_recov_objs(psc_target_t *pt, daos_rank_t recovered)
{
	cl_target_t	*target = pt->pt_target;
	psc_target_t	*recov_pt;
	int		 total;
	int		 nobjs;
	int		 i;

	if (target->co_status != CL_COMP_ST_UP)
		return 0;

	D_DEBUG(DF_PL, "Check %d objects of target[%d] to recover target[%d]\n",
		pt->pt_nobjs, target->co_rank, recovered);

	recov_pt = psc_target_find(recovered);

	total = pt->pt_nobjs + pt->pt_nobjs_rb;

	for (i = nobjs = 0; i < total;) {
		psc_obj_t	*obj;
		bool		 recov;

		obj = &pt->pt_objs[i];
		recov = pl_map_obj_recover(pg_data.pg_map, &obj->po_os,
					   &obj->po_attr, recovered);
		if (!recov) {
			i++;
			continue;
		}

		obj->po_os.os_rank = recovered;
		psc_target_append_obj(recov_pt, obj, false);
		psc_target_del_obj(pt, i);
		total--;
		nobjs++;
	}
	return nobjs;
}

static void
psc_target_recover(daos_rank_t recovered)
{
	psc_target_t	*pts	= pg_data.pg_targets;
	int		 nobjs	= 0;
	int		 ntgs	= 0;
	int		 i;
	int		 rc;

	for (i = 0; i < pg_data.pg_ntargets; i++) {
		if (pts[i].pt_rank == recovered)
			continue;

		rc = psc_target_recov_objs(&pts[i], recovered);
		if (rc == 0)
			continue;

		D_PRINT("target[%d] recovered %d objects for target[%d]\n",
			pts[i].pt_target->co_rank, rc, recovered);
		nobjs += rc;
		ntgs++;
	}

	if (nobjs != 0)
		D_PRINT("Recover %d objects from %d targets\n", nobjs, ntgs);

}

static int
psc_target_change(psc_argument_t *args)
{
	struct pa_target_change	*tgc = &args->u.target_change;
	int			 i;
	int			 rc;

	for (i = 0; i < tgc->t_nops; i++) {
		PSC_PROMPT("Set target[%d] to %s\n", tgc->t_ops[i].rank,
			   cl_comp_state2name(tgc->t_ops[i].state));

		rc = cl_target_set_state(pg_data.pg_clmap, tgc->t_ops[i].rank,
					 tgc->t_ops[i].state);
		if (rc != 0) {
			D_ERROR("Failed to change target[%d] status to %s\n",
				tgc->t_ops[i].rank,
				cl_comp_state2name(tgc->t_ops[i].state));
			break;
		}

		if (tgc->t_ops[i].state == CL_COMP_ST_DOWN)
			psc_target_rebuild(tgc->t_ops[i].rank);
		else
			psc_target_recover(tgc->t_ops[i].rank);

		if (tgc->t_print)
			psc_targets_print(0, NULL);

		psc_obj_stats_print();
	}

	if (i == 0 && tgc->t_print) /* print only */
		psc_targets_print(0, NULL);

	free(tgc->t_ops);
	return rc;
}

static int
psc_rebalance(psc_argument_t *args)
{
	psc_target_t *pst;
	int	      rebalanced;
	int	      i;
	int	      j;
	int	      rc;

	if (pg_data.pg_pcr.p_type == PL_TYPE_UNKNOWN ||
	    pg_data.pg_pcr.p_num == 0) {
		D_ERROR("Can't find valid placement map arguments\n");
		return -EPERM;
	}

	PSC_PROMPT("Rebuild placement map and Rebalance objects\n");

	/* recreate placement map */
	args->u.pl_create = pg_data.pg_pcr;
	rc = psc_pl_create(args);
	if (rc != 0)
		return rc;

	/* rebalance objects */
	for (i = 0; i < pg_data.pg_ntargets; i++) {
		psc_obj_t	 obj;
		daos_rank_t	 rebal;
		unsigned	 num;
		unsigned	 total;

		pst = &pg_data.pg_targets[i];
		total = pst->pt_nobjs;
		for (j = num = 0; j < pst->pt_nobjs; ) {
			obj = pst->pt_objs[j];

			rc = pl_map_obj_rebalance(pg_data.pg_map, &obj.po_os,
						  &obj.po_attr, &rebal);
			if (rc != 0)
				return rc;

			if (rebal == pst->pt_rank) {
				j++;
				continue;
			}

			D_DEBUG(DF_PL,
				"move "DF_U64"."DF_U64".%d from %d to %d\n",
				obj.po_os.os_id.body[1],
				obj.po_os.os_id.body[0],
				obj.po_os.os_sid, pst->pt_rank, rebal);

			obj.po_os.os_rank = rebal;
			psc_target_del_obj(pst, j);
			psc_target_append_obj(psc_target_find(rebal), &obj,
					      true);
			num++;
		}
		if (num != 0) {
			D_PRINT("Target %d moved out %d/%d objects\n",
				pst->pt_rank, num, total);
		}
	}

	for (i = rebalanced = 0; i < pg_data.pg_ntargets; i++) {
		pst = &pg_data.pg_targets[i];
		pst->pt_nobjs += pst->pt_nobjs_rb;
		rebalanced += pst->pt_nobjs_rb;
		pst->pt_nobjs_rb = 0;
	}
	psc_targets_print(0, NULL);
	psc_obj_stats_print();
	D_PRINT("Rebalanced %-5.2f%% of all objects\n",
		(float)(rebalanced * 100) / pg_data.pg_nobjs_m);

	return 0;
}

static void psc_sig_handler (int sig)
{
	wake_up = true;
}

static struct option psc_opts[] = {
	{ "cl_update",		required_argument,	NULL,	'C'},
	{ "pl_create",		required_argument,	NULL,	'P'},
	{ "obj_schema",		required_argument,	NULL,	'S'},
	{ "obj_create",		required_argument,	NULL,	'O'},
	{ "target_change",	required_argument,	NULL,	'T'},
	{ "rebuild",		optional_argument,	NULL,	'R'},
	{  NULL,		0,			NULL,	 0 }
};

/*
 * Usage:
 * -C	cl_update
 *  	create or update cluster map
 *  	r:N	N racks
 *  	l:N	N blades
 *  	b:N	N boards
 *  	n:N	N nodes
 *  	t:N	N targets
 *  	p	print cluster map
 *
 *  	e.g., b:4:t:16, create cluster map with 4 boards and 16 targets
 *
 * -P	create placement map
 *  	t:r	rim placement type
 *  	d:T	domain type, T can be r(rack) l(blade) b(board) n(node)
 *  		t(target)
 *  	n:N	number of rims
 *  	p	print placement map
 *
 * -S	Set object schema
 *  	s:N	stripe count
 *  	r:N	redundancy group size
 *  	s:N	number of spare nodes between two redundancy group
 *  	k:N	max skip distance for spare nodes
 *
 * -O	Create object
 *  	n:N	number of objects
 *  	p	print object layout
 *
 * -T	chanage status of target
 *  	d:N	disable target N
 *  	e:N	enable target N
 *  	p	print object stats in all targets
 *
 * -R	recreate placement map and rebalance objects
 *
 * Example:
 * DAOS_DEBUG=0
 * pseudo_cluster -C b:16,t:64,p	\
 * 	          -P t:r,d:b,n:1,p	\
 * 		  -S s:4,r:4,k:4	\
 * 		  -O n:40960,i:8	\
 * 		  -T d:60,e:60,p
 *
 * TODO: rebuild objects for changed cluster map
 */
int
main(int argc, char **argv)
{
	cl_map_t	 *clmap;
	char		 *feats;
	psc_argument_t	  args;
	struct sigaction  sa_bak;
	struct sigaction  sa;
	int		  dmask = 0;
	int		  opc;
	int		  rc;

	memset(&pg_data, 0, sizeof(pg_data));
	pg_data.pg_oid_gen = 1;

	memset(&args, 0, sizeof(args));

	sigemptyset(&sa.sa_mask);
	sa.sa_handler = psc_sig_handler;
	sa.sa_flags = 0;

	feats = getenv(PSC_PAUSE_MODE);
	if (feats != NULL) {
		if (strcasecmp(feats, "yes") == 0)
			pause_mode = true;
	}

	if (pause_mode)
		sigaction(SIGINT, &sa, &sa_bak);

	while ((opc = getopt_long(argc, argv, "C:P:O:T:S:R::",
				  psc_opts, NULL)) != -1) {
		if (opc == -1)
			break;

		args.str = optarg;
		switch (opc) {
		case 'C':
			dmask = DF_CL;
			rc = psc_cl_parse_args(optarg, &args);
			if (rc != 0)
				goto failed;

			if (pg_data.pg_clmap == NULL)
				rc = psc_cl_create(&args);
			else
				rc = psc_cl_change(&args);

			if (rc != 0)
				goto failed;

			clmap = pg_data.pg_clmap;
			rc = psc_targets_setup(cl_map_ntargets(clmap),
					       cl_map_targets(clmap));
			if (rc != 0) {
				D_ERROR("Failed to create pseudo targets\n");
				goto failed;
			}
			break;
		case 'P':
			dmask = DF_CL;
			rc = psc_pl_parse_args(optarg, &args);
			if (rc != 0)
				goto failed;

			rc = psc_pl_create(&args);
			if (rc != 0)
				goto failed;

			break;
		case 'S':
			dmask = DF_PL;
			rc = psc_obj_schema_args(optarg, &args);
			if (rc != 0)
				goto failed;
			break;

		case 'O':
			dmask = DF_PL;
			rc = psc_obj_create_args(optarg, &args);
			if (rc != 0)
				goto failed;

			rc = osc_obj_create(&args);
			if (rc != 0)
				goto failed;
			break;
		case 'T':
			dmask = DF_PL;
			rc = psc_target_change_args(optarg, &args);
			if (rc != 0)
				goto failed;

			rc = psc_target_change(&args);
			if (rc != 0)
				goto failed;
			break;
		case 'R':
			rc = psc_rebalance(&args);
			if (rc != 0)
				goto failed;
			break;
		}
		memset(&args, 0, sizeof(args));
	}
 failed:
	if (pause_mode)
		sigaction(SIGSTOP, &sa_bak, NULL);

	if (rc != 0)
		D_ERROR("Test failed %d!\n", rc);

	psc_targets_destroy();
	if (pg_data.pg_clmap != NULL)
		cl_map_destroy(pg_data.pg_clmap);

	if (pg_data.pg_map != NULL)
		pl_map_destroy(pg_data.pg_map);

	return rc;
}

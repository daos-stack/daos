/**
 * This file is part of daos_sr
 *
 * dsr/placement/pseudo_cl_map.c
 *
 * Author: Liang Zhen <liang.zhen@intel.com>
 */
#include <cl_map.h>
#include <pseudo_cl_buf.h>

static int
pseudo_domains_add_children(cl_domain_t *domains, unsigned int ndomains,
			    cl_domain_t *children, unsigned int nchildren,
			    daos_rank_t rank, cl_comp_type_t child_type)
{
	cl_target_t	*targets;
	int		 ntargets;
	int		 nc;
	int		 nt;
	int		 i;
	int		 j;

	D_ASSERT(nchildren >= ndomains);

	if (domains[0].cd_comp.co_type >= child_type) {
		D_DEBUG(DF_CL,
			"Parent type should be smaller than child: %s/%s\n",
			cl_domain_name(&domains[0]),
			cl_comp_type2name(child_type));
		return -EINVAL;
	}

	for (i = 0; i < ndomains; i++) {
		nc = nchildren / (ndomains - i);
		domains[i].cd_nchildren	= nc;
		domains[i].cd_children	= children;

		ntargets = domains[i].cd_ntargets;
		targets  = domains[i].cd_targets;

		D_DEBUG(DF_CL, "setup %d %ss under %s[%d]\n",
			nc, cl_comp_type2name(child_type),
			cl_domain_name(&domains[i]),
			domains[i].cd_comp.co_rank);

		for (j = 0; j < nc; j++) {
			nt = ntargets / (nc - j);

			D_DEBUG(DF_CL, "\tsetup %d targets under %s[%d]\n",
				nt, cl_comp_type2name(child_type), rank);

			children[j].cd_comp.co_rank   = rank++;
			children[j].cd_comp.co_type   = child_type;
			children[j].cd_comp.co_status = CL_COMP_ST_UNKNOWN;
			children[j].cd_targets	      = targets;
			children[j].cd_ntargets	      = nt;

			targets += nt;
			ntargets -= nt;
		}
		D_ASSERTF(ntargets == 0, "ntargets: %d\n", ntargets);

		children += nc;
		nchildren -= nc;
	}
	D_ASSERT(nchildren == 0);
	return 0;
}

int
cl_pseudo_buf_build(unsigned int ndesc, cl_pseudo_comp_desc_t *desc,
		    bool root, cl_buf_t **buf_pp)
{
	cl_buf_t	*buf;
	cl_domain_t	*domains;
	cl_domain_t	*children;
	cl_target_t	*targets;
	unsigned int	 ntargets;
	unsigned int	 ndomains;
	int		 i;
	int		 rc;

	if (ndesc < 2) {
		D_DEBUG(DF_CL, "Need at least two descriptors\n");
		return -EINVAL;
	}

	if (desc[0].cd_type >= CL_COMP_TARGET ||
	    desc[0].cd_type <= CL_COMP_ROOT) {
		D_DEBUG(DF_CL, "Invalid top level domain: %s\n",
			cl_comp_type2name(desc[0].cd_type));
		return -EINVAL;
	}

	if (desc[ndesc - 1].cd_type != CL_COMP_TARGET) {
		D_DEBUG(DF_CL, "Leaf type should be target/%s\n",
			cl_comp_type2name(desc[ndesc - 1].cd_type));
		return -EINVAL;
	}

	ntargets = desc[ndesc - 1].cd_number;
	D_DEBUG(DF_CL, "Total %d domain levels, %d targets\n",
		ndesc - 1, ntargets);

	for (ndomains = i = 0; i < ndesc - 1; i++)
		ndomains += desc[i].cd_number;

	ndomains += 1; /* for root */
	buf = calloc(1, ndomains * sizeof(cl_domain_t) +
			ntargets * sizeof(cl_target_t));
	if (buf == NULL) {
		D_DEBUG(DF_CL, "cannot allocate cluster components\n");
		rc = -ENOMEM;
		goto failed;
	}

	domains = buf;
	targets = (cl_target_t *)&domains[ndomains];
	for (i = 0; i < ntargets; i++) {
		targets[i].co_rank	= desc[ndesc - 1].cd_rank + i;
		targets[i].co_type	= CL_COMP_TARGET;
		targets[i].co_status	= CL_COMP_ST_UNKNOWN;
	}

	ndomains = 1;
	children = domains + 1;

	domains[0].cd_comp.co_rank	= 0;
	domains[0].cd_comp.co_type	= root ? CL_COMP_ROOT : CL_COMP_DUMMY;
	domains[0].cd_comp.co_status	= CL_COMP_ST_UP;
	domains[0].cd_ntargets		= ntargets;
	domains[0].cd_targets		= targets;

	for (i = 0; i < ndesc - 1; i++) {
		rc = pseudo_domains_add_children(domains, ndomains, children,
						 desc[i].cd_number,
						 desc[i].cd_rank,
						 desc[i].cd_type);
		if (rc < 0)
			goto failed;

		domains = children;
		ndomains = desc[i].cd_number;
		children += ndomains;
	}
	*buf_pp = buf;
	return 0;
 failed:
	cl_pseudo_buf_free(buf);
	return rc;
}

void
cl_pseudo_buf_free(cl_buf_t *buf)
{
	free(buf);
}

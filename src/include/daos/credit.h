/**
 * (C) Copyright 2021-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CREDIT_H__
#define __CREDIT_H__

#if D_HAS_WARNING(4, "-Wframe-larger-than=")
	#pragma GCC diagnostic ignored "-Wframe-larger-than="
#endif

#define DTS_KEY_LEN		64

/**
 * I/O credit, the utility can only issue \a ts_credits_avail concurrent I/Os,
 * each credit can carry all parameters for the asynchronous I/O call.
 */
struct io_credit {
	/** value buffer address */
	char			*tc_vbuf;
	/** dkey buffer */
	char			 tc_dbuf[DTS_KEY_LEN];
	/** akey buffer */
	char			 tc_abuf[DTS_KEY_LEN];
	/** dkey iov */
	daos_key_t		 tc_dkey;
	/** value iov */
	d_iov_t			 tc_val;
	/** sgl for the value iov */
	d_sg_list_t		 tc_sgl;
	/** I/O descriptor for input akey */
	daos_iod_t		 tc_iod;
	/** recx for the I/O, there is only one recx in \a tc_iod */
	daos_recx_t		 tc_recx;
	/** daos event for I/O */
	daos_event_t		 tc_ev;
	/** points to \a tc_ev in async mode, otherwise it's NULL */
	daos_event_t		*tc_evp;
};

#define DTS_CRED_MAX		1024
/**
 * I/O test context
 * It is input parameter which carries pool and container uuid etc, and output
 * parameter which returns pool and container open handle.
 *
 * If \a tsc_pmem_file is set, then it is VOS I/O test context, otherwise
 * it is DAOS I/O test context and \a ts_svc should be set.
 */
struct credit_context {
	/** INPUT: should be initialized by caller */
	/** pmem file name, only for VOS test */
	char			*tsc_pmem_file;
	/** pmem file root directory, only for VOS test */
	char			*tsc_pmem_path;
	/** DMG config file */
	char			*tsc_dmg_conf;
	/** optional, pool service ranks, only for DAOS test */
	d_rank_list_t		 tsc_svc;
	/** MPI rank of caller */
	int			 tsc_mpi_rank;
	/** # processes in the MPI program */
	int			 tsc_mpi_size;
	uuid_t			 tsc_pool_uuid;	/**< pool uuid */
	uuid_t			 tsc_cont_uuid;	/**< container uuid */
	/** pool SCM partition size */
	uint64_t		 tsc_scm_size;
	/** pool NVMe partition size */
	uint64_t		 tsc_nvme_size;
	/** number of I/O credits (tsc_credits) */
	int			 tsc_cred_nr;
	/** value size for \a tsc_credits */
	int			 tsc_cred_vsize;
	/** if pool/cont already created then can skip internal creation */
	bool			 tsc_skip_pool_create;
	bool			 tsc_skip_cont_create;
	/** INPUT END */

	/** OUTPUT: initialized within \a dts_ctx_init() */
	daos_handle_t		 tsc_poh;	/**< pool open handle */
	daos_handle_t		 tsc_coh;	/**< container open handle */
	daos_handle_t		 tsc_eqh;	/**< EQ handle */
	/** # available I/O credits */
	int			 tsc_cred_avail;
	/** # in-flight I/O credits */
	int			 tsc_cred_inuse;
	/** all pre-allocated I/O credits */
	struct io_credit	 tsc_cred_buf[DTS_CRED_MAX];
	/** pointers of all available I/O credits */
	struct io_credit	*tsc_credits[DTS_CRED_MAX];
	/** initialization steps, internal use only */
	int			 tsc_init;
	struct io_engine	*tsc_engine;
	/** OUTPUT END */
};

struct io_engine {
	char	*ie_name;
	int	(*ie_init)(struct credit_context *);
	void	(*ie_fini)(struct credit_context *);
	int	(*ie_pool_init)(struct credit_context *);
	void	(*ie_pool_fini)(struct credit_context *);
	int	(*ie_cont_init)(struct credit_context *);
	void	(*ie_cont_fini)(struct credit_context *);
};

int credits_init(struct credit_context *tsc);

void credits_fini(struct credit_context *tsc);

/**
 * Try to obtain a free credit from the I/O context.
 */
struct io_credit *credit_take(struct credit_context *tsc); /* OK */

/**
 * Drain all the in-flight I/O credits of @tsc.
 */
int credit_drain(struct credit_context *tsc);

/** return an unused credit */
void credit_return(struct credit_context *tsc, struct io_credit *cred);

extern struct io_engine vos_engine;

#endif /* __CREDIT_H__ */

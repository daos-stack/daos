/**
 * (C) Copyright 2015-2018 Intel Corporation.
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

#ifndef __DTS_COMMON_H__
#define __DTS_COMMON_H__

#define DTS_KEY_LEN		64

/**
 * I/O credit, the utility can only issue \a ts_credits_avail concurrent I/Os,
 * each credit can carry all parameters for the asynchronous I/O call.
 */
struct dts_io_credit {
	char			*tc_vbuf;	/**< value buffer address */
	char			 tc_dbuf[DTS_KEY_LEN];	/**< dkey buffer */
	char			 tc_abuf[DTS_KEY_LEN];	/**< akey buffer */
	daos_key_t		 tc_dkey;		/**< dkey iov */
	daos_iov_t		 tc_val;		/**< value iov */
	/** sgl for the value iov */
	daos_sg_list_t		 tc_sgl;
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
struct dts_context {
	/** INPUT: should be initialized by caller */
	/** optional, pmem file name, only for VOS test */
	char			*tsc_pmem_file;
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
	/** INPUT END */

	/** OUTPUT: initialized within \a dts_ctx_init() */
	daos_handle_t		 tsc_poh;	/**< pool open handle */
	daos_handle_t		 tsc_coh;	/**< container open handle */
	daos_handle_t		 tsc_eqh;	/**< EQ handle */
	/** # available I/O credits */
	int			 tsc_cred_avail;
	/** # inflight I/O credits */
	int			 tsc_cred_inuse;
	/** all pre-allocated I/O credits */
	struct dts_io_credit	 tsc_cred_buf[DTS_CRED_MAX];
	/** pointers of all available I/O credits */
	struct dts_io_credit	*tsc_credits[DTS_CRED_MAX];
	/** initialization steps, internal use only */
	int			 tsc_init;
	/** OUTPUT END */
};

/**
 * Initialize I/O test context:
 * - create and connect to pool based on the input pool uuid and size
 * - create and open container based on the input container uuid
 */
int dts_ctx_init(struct dts_context *tsc);
/**
 * Finalize I/O test context:
 * - close and destroy the test container
 * - disconnect and destroy the test pool
 */
void dts_ctx_fini(struct dts_context *tsc);

/**
 * Try to obtain a free credit from the I/O context.
 */
struct dts_io_credit *dts_credit_take(struct dts_context *tsc);
/**
 * Drain all the inflight I/O credits of @tsc.
 */
int dts_credit_drain(struct dts_context *tsc);

#endif /* __DTS_COMMON_H__ */

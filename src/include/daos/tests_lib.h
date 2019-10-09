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

#ifndef __DAOS_TESTS_LIB_H__
#define __DAOS_TESTS_LIB_H__

#include <getopt.h>
#include <daos_types.h>
#include <daos/object.h>

/** Read a command line from stdin. */
char *dts_readline(const char *prompt);

/** release a line buffer returned by dts_readline */
void  dts_freeline(char *line);

/** Fill in readable random bytes into the buffer */
void dts_buf_render(char *buf, unsigned int buf_len);

/** Fill in random uppercase chars into the buffer */
void dts_buf_render_uppercase(char *buf, unsigned int buf_len);

/** generate a unique key */
void dts_key_gen(char *key, unsigned int key_len, const char *prefix);

/** generate a random and unique object ID */
daos_obj_id_t dts_oid_gen(uint16_t oclass, uint8_t ofeats, unsigned seed);

/** generate a random and unique baseline object ID */
daos_unit_oid_t dts_unit_oid_gen(uint16_t oclass, uint8_t ofeats,
				 uint32_t shard);

/** Set rank into the oid */
#define dts_oid_set_rank(oid, rank)	daos_oclass_sr_set_rank(oid, rank)
/** Set target offset into oid */
#define dts_oid_set_tgt(oid, tgt)	daos_oclass_st_set_tgt(oid, tgt)

/**
 * Create a random (optionally) ordered integer array with \a nr elements, value
 * of this array starts from \a base.
 */
int *dts_rand_iarr_alloc(int nr, int base, bool shuffle);

static inline double
dts_time_now(void)
{
	struct timeval	tv;

	gettimeofday(&tv, NULL);
	return (tv.tv_sec + tv.tv_usec / 1000000.0);
}

/**
 * Readline a command line from stdin, parse and execute it.
 *
 * \param [IN]	opts		valid operations
 * \param [IN]	prompt		prompt string
 * \param [IN]	cmd_func	command functions
 */
int dts_cmd_parser(struct option *opts, const char *prompt,
		   int (*cmd_func)(char opc, char *args));

void dts_reset_key(void);

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
	d_iov_t			 tc_val;		/**< value iov */
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
/** Initialize and SGL with a variable number of IOVs and set the IOV buffers
 *  to the value of the strings passed
 *
 * @param sgl		Scatter gather list to initialize
 * @param count		Number of IO Vectors that will be created in the SGL
 * @param str		First string that will be used
 * @param ...		Rest of strings, up to count
 */
void
daos_sgl_init_with_strings(d_sg_list_t *sgl, uint32_t count, char *str, ...);

#endif /* __DAOS_TESTS_LIB_H__ */

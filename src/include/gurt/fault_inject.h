/*
 * (C) Copyright 2018-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/**
 * \file
 *
 * This file is part of gurt, it contains variables and functions for the  fault
 * injection feature.
 */

#ifndef __FAULT_INJECT__
#define __FAULT_INJECT__

#include <stdbool.h>
#include <yaml.h>
#include <stdint.h>
#include <gurt/debug.h>

/** @addtogroup GURT
 * @{
 */

#if defined(__cplusplus)
extern "C" {
#endif

/** Env to specify fault injection config file */
#define D_FAULT_CONFIG_ENV	"D_FI_CONFIG"

/** global on/off switch for fault injection */
extern unsigned int	d_fault_inject;
extern unsigned int	d_fault_config_file;

extern struct d_fault_attr_t *d_fault_attr_mem;
struct d_fault_attr_t {
	/**
	 * config id, used to select configuration from the fault_inject config
	 * file
	 */
	uint32_t		fa_id;
	/**
	 * inject faults every n-th occurrence. If interval is set to 5 and
	 * probability is set to 20, fault injection only occurs on every 5-th
	 * hit of fault_id with a 20% probability.
	 */
	uint32_t		fa_interval;
	/**
	 * max number of faults to inject. 0 means unlimited. After max_faults
	 * is reached, no faults will be injected for fault_id.
	 */
	uint64_t		fa_max_faults;
	/** counter of injected faults */
	uint64_t		fa_num_faults;
	/** number of times this injection point has been evaluated */
	uint64_t		fa_num_hits;
	/** argument string. Interpretation of content is up to the user */
	char			*fa_argument;
	/** spin lock to protect this struct */
	pthread_spinlock_t	fa_lock;
	/**
	 * the error code to inject. Can be retrieved by d_fault_attr_err_code()
	 */
	uint32_t		fa_err_code;
	/**
	 * state for nrand48. this allows each injection point has its own
	 * independent random number sequence.
	 */
	unsigned short		fa_rand_state[3];
	/**
	 * the frequency faults should be injected, calculated by:
	 *
	 *	freq = fa_probability_x / fa_probability_y
	 *
	 * e.g. fa_probability_x = 123, fa_probability_y = 1000
	 * means faults will be injected randomly with frequency 12.3%
	 */
	uint32_t			fa_probability_x;
	uint32_t			fa_probability_y;
};

/**
 * Initialize the fault injection framework, injection attributes are read from
 * the config file
 *
 * \return                   DER_SUCCESS on success, negative value on error
 */
int d_fault_inject_init(void);

/**
 * Finalize the fault injection framework
 *
 * \return                   DER_SUCCESS on success, negative value on error
 */
int d_fault_inject_fini(void);

/**
 * Start injecting faults.
 *
 * \return                   DER_SUCCESS on success, -DER_NOSYS if not supported
 */
int d_fault_inject_enable(void);

/**
 * Stop injecting faults.
 *
 * \return                   DER_SUCCESS on success, -DER_NOSYS if not supported
 */
int d_fault_inject_disable(void);

bool d_fault_inject_is_enabled(void);

bool d_should_fail(struct d_fault_attr_t *fault_attr_ptr);

/**
 * use this macro to determine if a fault should be injected at a specific call
 * site
 */
#define D_SHOULD_FAIL(fault_attr)			\
	({								\
		bool __rb;						\
		__rb = d_fault_inject && d_should_fail(fault_attr);	\
		if (__rb)						\
			D_WARN("fault_id %d, injecting fault.\n",	\
				fault_attr->fa_id);			\
		__rb;							\
	})

/**
 * initialize a fault attr.
 *
 * \param[in] fault_id          id of the fault
 * \param[in] fa_in             input fault attributes. Only the following
 *                              fields are used:
 *
 *                              fa_in.fa_probability
 *                              fa_in.fa_interval
 *                              fa_in.fa_max_faults
 *                              fa_in.fa_err_code
 *				fa_in.fa_num_faults
 *				fa_in.fa_probability_x
 *				fa_in.fa_probability_y
 * \return                      DER_SUCCESS on success, negative value on error.
 */
int
d_fault_attr_set(uint32_t fault_id, struct d_fault_attr_t fa_in);

/**
 * Retrieve the error code specified in the config file.
 *
 * \param[in] fault_id          id of the fault
 *
 * \return                      error code provided in the config file. 0 if no
 *                              error code was provided for fault_id.
 */
int
d_fault_attr_err_code(uint32_t fault_id);

/**
 * lookup the attributes struct address of a fault id.
 *
 * \param[in] fault_id          id of the fault
 *
 * \return                      address of the fault attributes for fault_id
 */
struct d_fault_attr_t *
d_fault_attr_lookup(uint32_t fault_id);

#if defined(__cplusplus)
}
#endif

/** @}
 */
#endif /* __FAULT_INJECT__ */

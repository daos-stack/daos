/* Copyright (C) 2018-2019 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/**
 * This file is part of gurt, it implements the fault injection feature.
 */

#define D_LOGFAC     DD_FAC(fi)
/** max length of argument string in the yaml config file */
#define FI_CONFIG_ARG_STR_MAX_LEN 4096

#define FI_MAX_FAULT_ID 8192

#include <gurt/common.h>

struct d_fi_gdata_t {
	unsigned int		  dfg_refcount;
	unsigned int		  dfg_inited;
	pthread_rwlock_t	  dfg_rwlock;
	struct d_fault_attr_t	**dfg_fa;
	uint32_t		  dfg_fa_capacity;
};

/**
 * global swith for fault injection. zero globally turns off fault injection,
 * non-zero turns on fault injection
 */
unsigned int			d_fault_inject;
unsigned int			d_fault_config_file;
static uint32_t			d_fault_inject_seed;
static struct d_fi_gdata_t	d_fi_gdata;
static pthread_once_t		d_fi_gdata_init_once = PTHREAD_ONCE_INIT;


static inline int
fault_attr_set(uint32_t fault_id, struct d_fault_attr_t fa_in, bool take_lock)
{
	struct d_fault_attr_t	 *fault_attr;
	struct d_fault_attr_t	 *new_fault_attr;
	struct d_fault_attr_t	**new_fa_arr;
	uint32_t		  new_capacity;
	void			 *start;
	size_t			  num_bytes;
	char			 *fa_argument = NULL;
	bool			  should_free = true;
	int			  rc = DER_SUCCESS;

	if (fault_id > FI_MAX_FAULT_ID) {
		D_ERROR("fault_id (%u) out of range [0, %d]\n", fault_id,
			FI_MAX_FAULT_ID);
		return -DER_INVAL;
	}

	D_ALLOC_PTR(new_fault_attr);
	if (new_fault_attr == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (fa_in.fa_argument) {
		D_STRNDUP(fa_argument, fa_in.fa_argument,
			  FI_CONFIG_ARG_STR_MAX_LEN);
		if (fa_argument == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (take_lock)
		D_RWLOCK_WRLOCK(&d_fi_gdata.dfg_rwlock);

	if (fault_id >= d_fi_gdata.dfg_fa_capacity) {
		new_capacity = fault_id + 1;
		D_REALLOC_ARRAY(new_fa_arr, d_fi_gdata.dfg_fa, new_capacity);
		if (new_fa_arr == NULL)
			D_GOTO(out_unlock, rc = -DER_NOMEM);

		start = new_fa_arr + d_fi_gdata.dfg_fa_capacity;
		num_bytes = sizeof(*new_fa_arr)
			    * (new_capacity - d_fi_gdata.dfg_fa_capacity);
		memset(start, 0, num_bytes);
		d_fi_gdata.dfg_fa = new_fa_arr;
		d_fi_gdata.dfg_fa_capacity = new_capacity;
	}

	fault_attr = d_fi_gdata.dfg_fa[fault_id];
	if (fault_attr == NULL) {
		fault_attr = new_fault_attr;

		rc = D_SPIN_INIT(&fault_attr->fa_lock,
				 PTHREAD_PROCESS_PRIVATE);
		if (rc != DER_SUCCESS)
			D_GOTO(out_unlock, rc);

		d_fi_gdata.dfg_fa[fault_id] = fault_attr;
		should_free = false;
	}

	D_SPIN_LOCK(&fault_attr->fa_lock);

	/* at this point, global lock is released, per entry lock is held */
	fault_attr->fa_id = fault_id;
	fault_attr->fa_probability_x = fa_in.fa_probability_x;
	fault_attr->fa_probability_y = fa_in.fa_probability_y;
	fault_attr->fa_interval = fa_in.fa_interval;
	fault_attr->fa_max_faults = fa_in.fa_max_faults;
	fault_attr->fa_err_code = fa_in.fa_err_code;
	fault_attr->fa_argument = fa_argument;
	/* nrand48() only takes the high order 48 bits for its seed */
	memcpy(fault_attr->fa_rand_state, &d_fault_inject_seed, 4);
	D_SPIN_UNLOCK(&fault_attr->fa_lock);

out_unlock:
	if (take_lock)
		D_RWLOCK_UNLOCK(&d_fi_gdata.dfg_rwlock);
out:
	if (should_free) {
		D_FREE(new_fault_attr);
		if (fa_in.fa_argument)
			D_FREE(fa_argument);
	}

	return rc;
}

int
d_fault_attr_set(uint32_t fault_id, struct d_fault_attr_t fa_in)
{
	return fault_attr_set(fault_id, fa_in, true);
}

int
d_fault_attr_err_code(uint32_t fault_id)
{
	struct d_fault_attr_t	*fault_attr;
	uint32_t		 err_code;

	if (fault_id >= d_fi_gdata.dfg_fa_capacity) {
		D_ERROR("fault id (%u) out of range [0, %u]\n",
			fault_id, d_fi_gdata.dfg_fa_capacity);
		return -DER_INVAL;
	}

	fault_attr = d_fi_gdata.dfg_fa[fault_id];
	if (fault_attr == NULL) {
		D_ERROR("fault id: %u not set.\n", fault_id);
		return -DER_INVAL;
	}

	D_SPIN_LOCK(&fault_attr->fa_lock);
	err_code = fault_attr->fa_err_code;
	D_SPIN_UNLOCK(&fault_attr->fa_lock);

	return (int) err_code;
}

static int
one_fault_attr_parse(yaml_parser_t *parser)
{
	yaml_event_t		 first;
	yaml_event_t		 second;
	struct d_fault_attr_t	 attr = { .fa_probability_x = 1,
					  .fa_probability_y = 1,
					  .fa_interval = 1 };
	const char		*id = "id";
	const char		*probability_x = "probability_x";
	const char		*probability_y = "probability_y";
	const char		*interval = "interval";
	const char		*max_faults = "max_faults";
	const char		*err_code = "err_code";
	const char		*argument = "argument";
	const char		*key_str;
	const char		*val_str;
	uint64_t		 val;
	int			 has_id = 0;
	int			 yaml_rc;
	int			 rc = DER_SUCCESS;

	do {
		/* libyaml functions return 1 on success, 0 on error */
		yaml_rc = yaml_parser_parse(parser, &first);
		if (yaml_rc != 1) {
			D_ERROR("yaml_parser_parse() failed. rc: %d\n",
				yaml_rc);
			D_GOTO(out, rc = -DER_MISC);
		}

		if (first.type == YAML_MAPPING_END_EVENT) {
			yaml_event_delete(&first);
			D_DEBUG(DB_ALL, "mapping end\n");
			break;
		}

		if (first.type != YAML_SCALAR_EVENT) {
			yaml_event_delete(&first);
			D_ERROR("Unknown element.\n");
			D_GOTO(out, rc = -DER_MISC);
		}

		yaml_rc = yaml_parser_parse(parser, &second);
		if (yaml_rc != 1) {
			yaml_event_delete(&first);
			D_ERROR("yaml_parser_parse() failed. rc: %d\n",
				yaml_rc);
			D_GOTO(out, rc = -DER_MISC);
		}

		if (second.type != YAML_SCALAR_EVENT) {
			yaml_event_delete(&first);
			yaml_event_delete(&second);
			D_ERROR("Unknown element.\n");
			D_GOTO(out, rc = -DER_MISC);
		}

		key_str = (char *) first.data.scalar.value;
		val_str = (const char *) second.data.scalar.value;
		val = strtoul(val_str, NULL, 10);
		if (!strcmp(key_str, id)) {
			D_DEBUG(DB_ALL, "id: %lu\n", val);
			attr.fa_id = val;
			has_id = 1;
		} else if (!strcmp(key_str, probability_x)) {
			attr.fa_probability_x = val;
			D_DEBUG(DB_ALL, "probability_x: %lu\n", val);
		} else if (!strcmp(key_str, probability_y)) {
			attr.fa_probability_y = val;
			D_DEBUG(DB_ALL, "probability_y: %lu\n", val);
		} else if (!strcmp(key_str, interval)) {
			attr.fa_interval = val;
			D_DEBUG(DB_ALL, "interval: %lu\n", val);
		} else if (!strcmp(key_str, max_faults)) {
			attr.fa_max_faults = val;
			D_DEBUG(DB_ALL, "max_faults: %lu\n", val);
		} else if (!strcmp(key_str, err_code)) {
			attr.fa_err_code = val;
			D_DEBUG(DB_ALL, "err_code: %lu\n", val);
		} else if (!strcmp(key_str, argument)) {
			D_STRNDUP(attr.fa_argument, val_str,
				  FI_CONFIG_ARG_STR_MAX_LEN);
			if (attr.fa_argument == NULL)
				rc = -DER_NOMEM;
			D_DEBUG(DB_ALL, "argument: %s\n", attr.fa_argument);

		} else {
			D_ERROR("Unknown key: %s\n", key_str);
			rc = -DER_MISC;
		}

		yaml_event_delete(&first);
		yaml_event_delete(&second);
		if (rc != -DER_SUCCESS)
			D_GOTO(out, rc);
	} while (1);

	if (!has_id) {
		D_ERROR("Fault config file item missing ID field.\n");
		D_GOTO(out, rc = -DER_MISC);
	}

	rc = fault_attr_set(attr.fa_id, attr, true);
	if (rc != DER_SUCCESS)
		D_ERROR("d_set_fault_attr(%u) failed, rc %d\n", attr.fa_id,
			rc);
out:
	D_FREE(attr.fa_argument);

	return rc;
}

static int
fault_attr_parse(yaml_parser_t *parser)
{
	yaml_event_t		event;
	yaml_event_type_t	event_type;
	int			yaml_rc;
	int			rc = -DER_SUCCESS;

	do {
		/* libyaml functions return 1 on success, 0 on error */
		yaml_rc = yaml_parser_parse(parser, &event);
		if (yaml_rc != 1) {
			D_ERROR("yaml_parser_parse() failed. rc: %d\n",
				yaml_rc);
			yaml_event_delete(&event);
			return -DER_MISC;
		}

		event_type = event.type;
		switch (event_type) {
		case YAML_MAPPING_START_EVENT:
			rc = one_fault_attr_parse(parser);
			if (rc != DER_SUCCESS) {
				D_ERROR("yaml_parser_parse() failed. "
					"rc: %d\n", rc);
			}
			break;
		default:
			break;
		}

		yaml_event_delete(&event);
		if (event_type == YAML_SEQUENCE_END_EVENT)
			break;
		if (rc != DER_SUCCESS)
			break;
	} while (1);

	return rc;
}

static int
seed_parse(yaml_parser_t *parser)
{
	yaml_event_t	 event;
	const char	*val_str;
	int		 yaml_rc;
	int		 rc = DER_SUCCESS;

	/* libyaml functions return 1 on success, 0 on error */
	yaml_rc = yaml_parser_parse(parser, &event);
	if (yaml_rc != 1) {
		D_ERROR("yaml_parser_parse() failed. rc: %d\n", yaml_rc);
		return -DER_MISC;
	}

	if (event.type != YAML_SCALAR_EVENT)
		D_GOTO(out, rc = -DER_INVAL);

	val_str = (const char *) event.data.scalar.value;
	d_fault_inject_seed = strtoul(val_str, NULL, 10);

out:
	yaml_event_delete(&event);

	return rc;
}

static void
d_fi_gdata_init(void)
{
	d_fi_gdata.dfg_refcount = 0;
	d_fi_gdata.dfg_inited = 1;
	D_RWLOCK_INIT(&d_fi_gdata.dfg_rwlock, NULL);
}

static void
d_fi_gdata_destroy(void)
{
	D_RWLOCK_DESTROY(&d_fi_gdata.dfg_rwlock);
	d_fi_gdata.dfg_refcount = 0;
	d_fi_gdata.dfg_inited = 0;
	d_fi_gdata.dfg_fa_capacity = 0;
	d_fi_gdata.dfg_fa = NULL;
}
/**
 * parse config file
 */
int
d_fault_inject_init(void)
{
	char			*config_file;
	FILE			*fp = NULL;
	yaml_parser_t		 parser;
	yaml_event_t		 event;
	yaml_event_type_t	 event_type;
	int			 last_errno;
	int			 yaml_rc;
	int			 rc = DER_SUCCESS;

	pthread_once(&d_fi_gdata_init_once, d_fi_gdata_init);
	D_ASSERT(d_fi_gdata.dfg_inited == 1);

	D_RWLOCK_WRLOCK(&d_fi_gdata.dfg_rwlock);
	d_fi_gdata.dfg_refcount++;
	if (d_fi_gdata.dfg_refcount > 1) {
		D_RWLOCK_UNLOCK(&d_fi_gdata.dfg_rwlock);
		return rc;
	}
	D_RWLOCK_UNLOCK(&d_fi_gdata.dfg_rwlock);

	config_file = getenv(D_FAULT_CONFIG_ENV);
	if (config_file == NULL || strlen(config_file) == 0) {
		D_INFO("No config file, fault injection is OFF.\n");
		D_GOTO(out, rc);
	}

	fp = fopen(config_file, "r");
	last_errno = errno;
	if (fp == NULL) {
		D_ERROR("Failed to open file %s (%s).\n",
			config_file, strerror(last_errno));
		rc = d_errno2der(last_errno);
		D_GOTO(out, rc);
	}

	yaml_rc = yaml_parser_initialize(&parser);
	if (yaml_rc != 1) {
		D_ERROR("Failed to intialize yaml parser. rc: %d\n", yaml_rc);
		D_GOTO(out, rc = -DER_MISC);
	}

	yaml_parser_set_input_file(&parser, fp);
	do {
		/* libyaml functions return 1 on success, 0 on error */
		yaml_rc = yaml_parser_parse(&parser, &event);
		if (yaml_rc != 1) {
			D_ERROR("yaml_parser_parse() failed. rc: %d\n",
				yaml_rc);
			D_GOTO(out, rc = -DER_MISC);
		}

		event_type = event.type;
		if (event_type == YAML_STREAM_END_EVENT) {
			yaml_event_delete(&event);
			break;
		}
		if (event_type != YAML_SCALAR_EVENT) {
			yaml_event_delete(&event);
			continue;
		}

		if (!strncmp((char *) event.data.scalar.value,
			     "fault_config", strlen("fault_config") + 1)) {
			rc = fault_attr_parse(&parser);
			if (rc != DER_SUCCESS)
				D_ERROR("fault_attr_parse() failed. rc %d\n",
					rc);
		} else if (!strncmp((char *) event.data.scalar.value,
				    "seed", strlen("seed") + 1)) {
			rc = seed_parse(&parser);
			if (rc != DER_SUCCESS)
				D_ERROR("seed_parse() failed. rc %d\n", rc);
		} else {
			D_ERROR("unknown key: %s\n", event.data.scalar.value);
			rc = -DER_INVAL;
		}

		yaml_event_delete(&event);
		if (rc != DER_SUCCESS)
			break;
	} while (1);

	yaml_parser_delete(&parser);
	if (rc == DER_SUCCESS) {
		D_INFO("Config file: %s, fault injection is ON.\n",
			config_file);
		d_fault_config_file = 1;
		d_fault_inject = 1;
		D_GOTO(out, rc);
	}

out:
	if (fp)
		fclose(fp);
	return rc;
}

int
d_fault_inject_fini()
{
	int	i;
	int	rc = 0;

	if (d_fi_gdata.dfg_inited == 0) {
		D_DEBUG(DB_TRACE, "fault injection not initialized.\n");
		return rc;
	}

	D_RWLOCK_WRLOCK(&d_fi_gdata.dfg_rwlock);
	d_fi_gdata.dfg_refcount--;
	if (d_fi_gdata.dfg_refcount != 0) {
		D_RWLOCK_UNLOCK(&d_fi_gdata.dfg_rwlock);
		return rc;
	}

	for (i = 0; i < d_fi_gdata.dfg_fa_capacity; i++) {
		int	local_rc;

		if (d_fi_gdata.dfg_fa[i] == NULL)
			continue;

		local_rc = D_SPIN_DESTROY(&d_fi_gdata.dfg_fa[i]->fa_lock);
		if (local_rc != DER_SUCCESS)
			D_ERROR("Can't destroy spinlock for fault id: %d\n", i);
		if (rc == 0 && local_rc)
			rc = local_rc;
		if (d_fi_gdata.dfg_fa[i]->fa_argument)
			D_FREE(d_fi_gdata.dfg_fa[i]->fa_argument);

		D_FREE(d_fi_gdata.dfg_fa[i]);
	}
	D_FREE(d_fi_gdata.dfg_fa);

	D_RWLOCK_UNLOCK(&d_fi_gdata.dfg_rwlock);
	d_fi_gdata_destroy();
	d_fi_gdata_init_once = PTHREAD_ONCE_INIT;

	D_DEBUG(DB_ALL, "Finalized.\n");

	return rc;
}


void
d_fault_inject_enable(void)
{
	if (!d_fault_config_file) {
		D_ERROR("No fault config file.\n");
		return;
	}

	d_fault_inject = 1;
}

void
d_fault_inject_disable(void)
{
	d_fault_inject = 0;
}

bool
d_fi_initialized()
{
	return d_fi_gdata.dfg_inited == 1;
}

/**
 * based on the state of fault_id, decide if a fault should be injected
 *
 * \param[in] fault_id       fault injection configuration id
 *
 * \return                   true if should inject fault, false if should not
 *                           inject fault
 *
 *                           support injecting X faults in Y occurances
 */
bool
d_should_fail(uint32_t fault_id)
{
	struct d_fault_attr_t	*fault_attr;
	bool			 rc = true;

	if (!d_fi_initialized()) {
		D_ERROR("fault injectiont not initalized.\n");
		return false;
	}

	/*
	 * based on the state of fault_attr, decide if a fault should
	 * be injected
	 */
	if (fault_id >= d_fi_gdata.dfg_fa_capacity) {
		D_ERROR("fault id (%u) out of range [0, %u]\n",
			fault_id, d_fi_gdata.dfg_fa_capacity - 1);
		return false;
	}

	fault_attr = d_fi_gdata.dfg_fa[fault_id];

	if (!fault_attr) {
		return false;
	}

	D_SPIN_LOCK(&fault_attr->fa_lock);
	if (fault_attr->fa_probability_x == 0)
		D_GOTO(out, rc = false);

	if (fault_attr->fa_max_faults != 0 &&
	    fault_attr->fa_max_faults <= fault_attr->fa_num_faults)
		D_GOTO(out, rc = false);

	if (fault_attr->fa_interval > 1) {
		fault_attr->fa_num_hits++;
		if (fault_attr->fa_num_hits % fault_attr->fa_interval)
			D_GOTO(out, rc = false);
	}

	if (fault_attr->fa_probability_y != 0 &&
	    fault_attr->fa_probability_x <=
	    nrand48(fault_attr->fa_rand_state) % fault_attr->fa_probability_y)
		D_GOTO(out, rc = false);

	fault_attr->fa_num_faults++;

out:
	D_SPIN_UNLOCK(&fault_attr->fa_lock);
	return rc;
};


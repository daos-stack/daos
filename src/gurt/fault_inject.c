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
 * This file is part of gurt, it implements the fault injection feature.
 */

#define D_LOGFAC     DD_FAC(fi)
/** max length of argument string in the yaml config file */
#define FI_CONFIG_ARG_STR_MAX_LEN 4096

/* (1 << D_FA_TABLE_BITS) is the number of buckets of fa hash table */
#define D_FA_TABLE_BITS		(13)

#include <gurt/common.h>
#include <gurt/hash.h>
#include "fi.h"

/**
 * global switch for fault injection. zero globally turns off fault injection,
 * non-zero turns on fault injection
 */
unsigned int			d_fault_inject;
unsigned int			d_fault_config_file;
struct d_fault_attr_t *d_fault_attr_mem;

#if FAULT_INJECTION

static struct d_fault_attr *
fa_link2ptr(d_list_t *rlink)
{
	D_ASSERT(rlink != NULL);
	return container_of(rlink, struct d_fault_attr, fa_link);
}

static bool
fa_op_key_cmp(struct d_hash_table *htab, d_list_t *rlink, const void *key,
	      unsigned int ksize)
{
	struct d_fault_attr *fa_ptr = fa_link2ptr(rlink);

	D_ASSERT(ksize == sizeof(uint32_t));

	return fa_ptr->fa_attr.fa_id == *(uint32_t *)key;
}

static uint32_t
fa_op_rec_hash(struct d_hash_table *htab, d_list_t *link)
{
	struct d_fault_attr *fa_ptr = fa_link2ptr(link);

	return d_hash_string_u32((const char *)&fa_ptr->fa_attr.fa_id,
				 sizeof(fa_ptr->fa_attr.fa_id));
}

static void
fa_op_rec_free(struct d_hash_table *htab, d_list_t *rlink)
{
	struct d_fault_attr	*ht_rec = fa_link2ptr(rlink);
	int			 rc;

	D_FREE(ht_rec->fa_attr.fa_argument);
	rc = D_SPIN_DESTROY(&ht_rec->fa_attr.fa_lock);
	if (rc != DER_SUCCESS)
		D_ERROR("Can't destroy spinlock for fault id: %d\n",
			ht_rec->fa_attr.fa_id);
	D_FREE(ht_rec);
}

/**
 * abuse hop_rec_decref() so that we can safely use it without a
 * hop_rec_addref(). The goal is to have d_hash_table_destroy_inplace()
 * destroy all records automatically.
 */
static bool
fa_op_rec_decref(struct d_hash_table *htab, d_list_t *rlink)
{
	return true;
}

static d_hash_table_ops_t fa_table_ops = {
	.hop_key_cmp	= fa_op_key_cmp,
	.hop_rec_hash	= fa_op_rec_hash,
	.hop_rec_decref	= fa_op_rec_decref,
	.hop_rec_free	= fa_op_rec_free,
};

struct d_fi_gdata_t {
	unsigned int		  dfg_refcount;
	unsigned int		  dfg_inited;
	pthread_rwlock_t	  dfg_rwlock;
	struct d_hash_table	  dfg_fa_table;
};

/**
 * global switch for fault injection. zero globally turns off fault injection,
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
	char			 *fa_argument = NULL;
	bool			  should_free = true;
	struct d_fault_attr	 *new_rec = NULL;
	struct d_fault_attr	 *rec = NULL;
	d_list_t		 *rlink = NULL;
	int			  rc = DER_SUCCESS;

	D_ALLOC_PTR(new_rec);
	if (new_rec == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (fa_in.fa_argument) {
		D_STRNDUP(fa_argument, fa_in.fa_argument,
			  FI_CONFIG_ARG_STR_MAX_LEN);
		if (fa_argument == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (take_lock)
		D_RWLOCK_WRLOCK(&d_fi_gdata.dfg_rwlock);

	rlink = d_hash_rec_find_insert(&d_fi_gdata.dfg_fa_table, &fault_id,
				sizeof(fault_id), &new_rec->fa_link);
	if (rlink == &new_rec->fa_link) {
		fault_attr = &new_rec->fa_attr;
		rc = D_SPIN_INIT(&fault_attr->fa_lock,
				 PTHREAD_PROCESS_PRIVATE);
		if (rc != DER_SUCCESS)
			D_GOTO(out_unlock, rc);
		D_DEBUG(DB_ALL, "new fault id: %u added.\n", fault_id);
		should_free = false;
	} else {
		rec = fa_link2ptr(rlink);
		D_ASSERT(rec->fa_attr.fa_id == fault_id);
		fault_attr = &rec->fa_attr;
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
	/**
	 * Let's update fa_num_faults here too, so the user can reset num faults
	 * by fault_attr_set, then it can use the same fault_attr to inject
	 * other failures.
	 */
	fault_attr->fa_num_faults = fa_in.fa_num_faults;
	/* nrand48() only takes the high order 48 bits for its seed */
	memcpy(fault_attr->fa_rand_state, &d_fault_inject_seed, 4);
	D_SPIN_UNLOCK(&fault_attr->fa_lock);

out_unlock:
	if (take_lock)
		D_RWLOCK_UNLOCK(&d_fi_gdata.dfg_rwlock);
out:
	if (should_free) {
		D_FREE(new_rec);
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

struct d_fault_attr_t *
d_fault_attr_lookup(uint32_t fault_id)
{
	struct d_fault_attr_t	*fault_attr;
	struct d_fault_attr	*ht_rec;
	d_list_t		*rlink;

	D_RWLOCK_RDLOCK(&d_fi_gdata.dfg_rwlock);
	rlink = d_hash_rec_find(&d_fi_gdata.dfg_fa_table, (void *)&fault_id,
				sizeof(fault_id));
	D_RWLOCK_UNLOCK(&d_fi_gdata.dfg_rwlock);
	if (rlink == NULL) {
		D_DEBUG(DB_ALL, "fault attr for fault ID %d not set yet.\n",
			fault_id);
		fault_attr = NULL;
	} else {
		ht_rec = fa_link2ptr(rlink);
		D_ASSERT(ht_rec->fa_attr.fa_id == fault_id);
		fault_attr = &ht_rec->fa_attr;
	}

	return fault_attr;
}

int
d_fault_attr_err_code(uint32_t fault_id)
{
	struct d_fault_attr_t	*fault_attr;
	int32_t			 err_code;

	fault_attr = d_fault_attr_lookup(fault_id);
	if (fault_attr == NULL) {
		D_ERROR("fault id: %u not set.\n", fault_id);
		return -DER_INVAL;
	}

	D_SPIN_LOCK(&fault_attr->fa_lock);
	err_code = fault_attr->fa_err_code;
	D_SPIN_UNLOCK(&fault_attr->fa_lock);

	return err_code;
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
		val = strtoul(val_str, NULL, 0);
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
			attr.fa_err_code = strtol(val_str, NULL, 0);
			D_DEBUG(DB_ALL, "err_code: " DF_RC "\n",
				DP_RC(attr.fa_err_code));
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
	int rc;

	d_fi_gdata.dfg_refcount = 0;
	d_fi_gdata.dfg_inited = 1;
	D_RWLOCK_INIT(&d_fi_gdata.dfg_rwlock, NULL);
	rc =  d_hash_table_create_inplace(D_HASH_FT_NOLOCK, D_FA_TABLE_BITS,
					  NULL, &fa_table_ops,
					  &d_fi_gdata.dfg_fa_table);
	if (rc != 0)
		D_ERROR("d_hash_table_create_inplace() failed, rc: %d.\n", rc);
}

static void
d_fi_gdata_destroy(void)
{
	int rc;

	rc = d_hash_table_destroy_inplace(&d_fi_gdata.dfg_fa_table,
					  true /* force */);
	if (rc != 0) {
		D_ERROR("failed to destroy fault attr data. force: %d, "
			"d_hash_table_destroy_inplace failed, rc: %d\n",
			true, rc);
	}
	D_RWLOCK_DESTROY(&d_fi_gdata.dfg_rwlock);
	d_fi_gdata.dfg_refcount = 0;
	d_fi_gdata.dfg_inited = 0;
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
		D_ERROR("Failed to initialize yaml parser. rc: %d\n", yaml_rc);
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
	} else {
		D_ERROR("Failed to parse fault config file.\n");
		D_GOTO(out, rc);
	}

	/* Register D_ALLOC() hook as fault ID zero, but do not check
	 * for failure as it will fail if no config file is provided
	 */
	d_fault_attr_mem = d_fault_attr_lookup(0);

out:
	if (fp)
		fclose(fp);
	return rc;
}

int
d_fault_inject_fini()
{
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

	D_RWLOCK_UNLOCK(&d_fi_gdata.dfg_rwlock);
	d_fi_gdata_destroy();
	d_fi_gdata_init_once = PTHREAD_ONCE_INIT;

	D_DEBUG(DB_ALL, "Finalized.\n");

	return rc;
}


int
d_fault_inject_enable(void)
{
	if (!d_fault_config_file) {
		D_ERROR("No fault config file.\n");
		return -DER_NOSYS;
	}

	d_fault_inject = 1;
	return 0;
}

int
d_fault_inject_disable(void)
{
	d_fault_inject = 0;
	return 0;
}

bool
d_fi_initialized()
{
	return d_fi_gdata.dfg_inited == 1;
}

bool
d_fault_inject_is_enabled(void)
{
	if (d_fault_inject)
		return true;
	return false;
}

/**
 * based on the state of fault_id, decide if a fault should be injected
 *
 * \param[in] fault_id       fault injection configuration id
 *
 * \return                   true if should inject fault, false if should not
 *                           inject fault
 *
 *                           support injecting X faults in Y occurrences
 */
bool
d_should_fail(struct d_fault_attr_t *fault_attr)
{
	bool			 rc = true;

	if (!d_fi_initialized()) {
		D_ERROR("fault injection not initialized.\n");
		return false;
	}

	/*
	 * based on the state of fault_attr, decide if a fault should
	 * be injected
	 */
	if (!fault_attr)
		return false;

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
#else /* FAULT_INJECT */
int d_fault_inject_init(void)
{
	D_WARN("Fault Injection not initialized feature not included in build");
	return -DER_NOSYS;
}

int d_fault_inject_fini(void)
{
	D_WARN("Fault Injection not finalized feature not included in build");
	return -DER_NOSYS;
}

int d_fault_inject_enable(void)
{
	D_WARN("Fault Injection not enabled feature not included in build");
	return -DER_NOSYS;
}

int d_fault_inject_disable(void)
{
	D_WARN("Fault Injection not disabled feature not included in build");
	return -DER_NOSYS;
}

bool
d_fault_inject_is_enabled(void)
{
	return false;
}

bool
d_should_fail(struct d_fault_attr_t *fault_attr)
{
	return false;
}

int
d_fault_attr_set(uint32_t fault_id, struct d_fault_attr_t fa_in)
{
	D_WARN("Fault Injection attr not set feature not included in build");
	return 0;
}

struct d_fault_attr_t *
d_fault_attr_lookup(uint32_t fault_id)
{
	return NULL;
}

int
d_fault_attr_err_code(uint32_t fault_id)
{
	return 0;
}
#endif /* FAULT_INJECT */

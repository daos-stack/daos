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

#define HAVE_LIB_READLINE	1

#if HAVE_LIB_READLINE
# include <readline/history.h>
# include <readline/readline.h>

#define dts_add_history(s)	add_history(s)
#else /* HAVE_LIB_READLINE */
# define dts_add_history(s)	do {} while (0)
#endif /* HAVE_LIB_READLINE */

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
 * Create a random ordered integer array with \a nr elements, value of this
 * array starts from \a base.
 */
int *dts_rand_iarr_alloc(int nr, int base);

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

#endif /* __DAOS_TESTS_LIB_H__ */

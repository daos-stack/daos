/* Copyright (C) 2016-2018 Intel Corporation
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

#include <ctype.h>
#include <errno.h>
#include <yaml.h>

#include "log.h"
#include "ionss.h"

/* The following code uses the
 * <a href="https://en.wikipedia.org/wiki/X_Macro">X Macro</a> pattern for
 * processing the list of start-up configuration options for the ionss, to
 * avoid redundant code and improve maintainability.
 *
 * The macro "X" is called/defined using the following arguments:
 *
 * X(file_key, parser_function, property_chain)
 *
 * file_key identifies the parameter within the YAML file.
 * parser_function depends on the type of data being parsed.
 * property_chain supplies the address where the data is to be saved.
 *
 * Call sites for the macro are expected to use only the required arguments
 * while disregarding the remainder using "...".
 */

/* Options that can be specified either per projection or globally.
 * If specified at both places, the per projection value takes precedence.
 */
#define COMMON_OPTIONS				\
	X(readdir_size, set_size)		\
	X(max_read_size, set_size)		\
	X(max_write_size, set_size)		\
	X(max_iov_read_size, set_size)		\
	X(max_iov_write_size, set_size)		\
	X(max_read_count, set_decimal)		\
	X(max_write_count, set_decimal)		\
	X(inode_htable_size, set_decimal)	\
	X(cnss_thread_count, set_decimal)	\
	X(cnss_timeout, set_decimal)		\
	X(cnss_threads, set_flag)		\
	X(fuse_read_buf, set_flag)		\
	X(fuse_write_buf, set_flag)		\
	X(failover, set_feature)		\
	X(writeable, set_feature)

#define GLOBAL_OPTIONS				\
	X(group_name, set_string)		\
	X(poll_interval, set_decimal)		\
	X(cnss_poll_interval, set_decimal)	\
	X(thread_count, set_decimal)		\
	X(progress_callback, set_flag)

#define PROJ_OPTIONS				\
	X(full_path, set_string, false)		\
	X(mount_path, set_string, true)

#define X(name, ...) const char *key_##name = #name;
COMMON_OPTIONS
GLOBAL_OPTIONS
PROJ_OPTIONS
#undef X

const char	*default_group_name		= "IONSS";
const uint32_t	default_thread_count		= 2;
const uint32_t	default_poll_interval		= (1000 * 1000);
const uint32_t	default_cnss_poll_interval	= (1);
const bool	default_progress_callback	= true;
const uint32_t	default_readdir_size		= (64 * 1024);
const uint32_t	default_max_read_size		= (1024 * 1024);
const uint32_t	default_max_write_size		= (1024 * 1024);
const uint32_t	default_max_iov_read_size	= 64;
const uint32_t	default_max_iov_write_size	= 64;
const uint32_t	default_max_read_count		= 3;
const uint32_t	default_max_write_count		= 3;
const uint32_t	default_inode_htable_size	= 5;
const uint32_t	default_cnss_thread_count	= 0;
const uint32_t	default_cnss_timeout		= 60;
const bool	default_cnss_threads		= true;
const bool	default_fuse_read_buf		= true;
const bool	default_fuse_write_buf		= true;
const bool	default_failover		= true;
const bool	default_writeable		= true;

struct parsed_option_s {
	const char *key;
	union {
		uint64_t		buf;
		bool			bool_val;
		uint32_t		uint_val;
		void			*ptr_val;
	};
	bool is_set;
	int (*setter)(struct parsed_option_s *,
		      yaml_document_t *, yaml_node_t *);
};

struct projections_s {
	int num_options;
	int num_projections;
	struct parsed_option_s **options;
};

static inline
struct parsed_option_s *find_option(struct parsed_option_s *options,
				    int num_options,
				    const char *key, int key_length)
{
	int i;

	for (i = 0; i < num_options; i++)
		if (strncmp(key, options[i].key, key_length) == 0)
			return (options + i);
	return NULL;
}

/*
 * Parse a uint32_t from a command line option and allow either k or m
 * suffixes.  Updates the value if str contains a valid value or returns
 * -1 on failure.
 */
static int parse_number(uint32_t *value, const char *str,
			int len, uint32_t multiplier)
{
	uint32_t new_value = *value;
	char fmt[10];

	/* Read the numeric value */
	snprintf(fmt, 10, "%%%du", len);
	if (sscanf(str, fmt, &new_value) != 1)
		return -1;

	/* Advance pch to the next non-numeric character */
	while (isdigit(*str))
		str++;

	switch (*str) {
	case '\0':
		break;
	case 'm':
	case 'M':
		new_value *= multiplier;
	case 'k':
	case 'K':
		new_value *= multiplier;
		break;
	default:
		IOF_LOG_ERROR("Invalid numeric data %s", str);
		return -1;
	}
	*value = new_value;
	IOF_LOG_DEBUG("Setting option value: %u", new_value);
	return 0;
}

static int set_decimal(struct parsed_option_s *option,
		       yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE) {
		IOF_LOG_ERROR("Invalid YAML node type");
		return -1;
	}
	return parse_number((uint32_t *)&option->uint_val,
			    (char *)node->data.scalar.value,
			    (int)node->data.scalar.length, 1000);
}

static int set_size(struct parsed_option_s *option,
		    yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE) {
		IOF_LOG_ERROR("Invalid YAML node type");
		return -1;
	}
	return parse_number((uint32_t *)&option->uint_val,
			    (char *)node->data.scalar.value,
			    (int)node->data.scalar.length, 1024);
}

static int parse_boolean(bool *value, char *str, int len, char *list[2])

{
	if (!strncasecmp(str, list[0], strlen(list[0]))) {
		*value = false;
	} else if (!strncasecmp(str, list[1], strlen(list[1]))) {
		*value = true;
	} else {
		IOF_LOG_ERROR("Invalid Feature Option: %.*s", len, str);
		return -1;
	}
	IOF_LOG_DEBUG("Setting option value: %.*s", len, str);
	return 0;
}

static int set_feature(struct parsed_option_s *option,
		       yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE) {
		IOF_LOG_ERROR("Invalid YAML node type");
		return -1;
	}
	return parse_boolean(&option->bool_val,
			     (char *)node->data.scalar.value,
			     (int)node->data.scalar.length,
			     (char*[]) { "disable", "auto" });
}

static int set_flag(struct parsed_option_s *option,
		    yaml_document_t *document, yaml_node_t *node)
{
	if (node->type != YAML_SCALAR_NODE) {
		IOF_LOG_ERROR("Invalid YAML node type");
		return -1;
	}
	return parse_boolean(&option->bool_val,
			     (char *)node->data.scalar.value,
			     (int)node->data.scalar.length,
			     (char*[]) { "false", "true" });
}

static int set_string(struct parsed_option_s *option,
		      yaml_document_t *document, yaml_node_t *node)
{
	int ret = 0;

	if (node->type != YAML_SCALAR_NODE) {
		IOF_LOG_ERROR("Invalid YAML node type");
		return -1;
	}
	D_STRNDUP(option->ptr_val,
		  ((char *)node->data.scalar.value),
		  node->data.scalar.length);
	if (option->ptr_val == NULL)
		ret = -1;
	else
		IOF_LOG_DEBUG("Setting option value: %.*s",
			      (int)node->data.scalar.length,
			      (char *)node->data.scalar.value);
	return ret;
}

static int parse_node(yaml_document_t *document, yaml_node_t *node,
		      struct parsed_option_s *options, int num_options)
{
	int ret;
	yaml_node_t *key_node, *val_node;
	yaml_node_pair_t *node_pair;
	struct parsed_option_s *sel_option;

	if (node->type != YAML_MAPPING_NODE) {
		IOF_LOG_ERROR("Invalid YAML node type");
		return -1;
	}
	for (node_pair = node->data.mapping.pairs.start;
	     node_pair < node->data.mapping.pairs.top; node_pair++) {
		key_node = yaml_document_get_node(document, node_pair->key);
		sel_option = find_option(options, num_options,
					 (char *)key_node->data.scalar.value,
					 key_node->data.scalar.length);
		if (!sel_option) {
			IOF_LOG_WARNING("Unknown configuration option %.*s",
					(int)key_node->data.scalar.length,
					(char *)key_node->data.scalar.value);
			return -1;
		}
		IOF_LOG_DEBUG("Processing configuration option: %s",
			      sel_option->key);
		val_node = yaml_document_get_node(document, node_pair->value);
		ret = sel_option->setter(sel_option, document, val_node);
		if (ret) {
			if (val_node->type == YAML_SCALAR_NODE) {
				IOF_LOG_WARNING("Unknown configuration value %s %.*s",
						sel_option->key,
						(int)val_node->data.scalar.length,
						(char *)val_node->data.scalar.value);
			} else {
				IOF_LOG_WARNING("Unknown configuration value %s",
						sel_option->key);
			}
			return ret;
		}
		sel_option->is_set = true;
	}
	return 0;
}

static int parse_projections(struct parsed_option_s *option,
			     yaml_document_t *document, yaml_node_t *node)
{
	int i, j, ret;
	yaml_node_t *next_node;
	yaml_node_item_t *node_item;
	struct projections_s *proj = option->ptr_val;

	if (node->type != YAML_SEQUENCE_NODE) {
		IOF_LOG_ERROR("Invalid YAML node type");
		return -1;
	}
	proj->num_options = 0;
#define X(...)	++proj->num_options;
	COMMON_OPTIONS
	PROJ_OPTIONS
#undef X
	proj->num_projections = (node->data.sequence.items.top
				 - node->data.sequence.items.start);
	D_ALLOC_ARRAY(proj->options, proj->num_projections);
	if (!proj->options)
		return -1;
	D_ALLOC_ARRAY(proj->options[0],
		      proj->num_projections * proj->num_options);
	if (!proj->options[0])
		return -1;

	for (i = 0; i < proj->num_projections; i++) {
		j = 0;
		proj->options[i] = proj->options[0] + i
				 * proj->num_options;
#define X(name, fn, ...)						\
	{								\
		proj->options[i][j].key = key_##name;			\
		proj->options[i][j].is_set = false;			\
		proj->options[i][j].setter = fn;			\
		j++;							\
	}
		COMMON_OPTIONS
		PROJ_OPTIONS
#undef X
	}
	IOF_LOG_INFO("Projecting %d exports", proj->num_projections);

	for (i = 0, node_item = node->data.sequence.items.start;
	     node_item < node->data.sequence.items.top; node_item++, i++) {
		next_node = yaml_document_get_node(document, *node_item);
		ret = parse_node(document, next_node,
				 proj->options[i], proj->num_options);
		if (ret != 0)
			return ret;
	}
	return 0;
}

int parse_config(char *path, struct ios_base *base)
{
	int i, ret = 0;
	FILE *fp = NULL;
	yaml_parser_t parser;
	yaml_document_t document;
	int parser_initialized = 0;
	int document_loaded = 0;

	struct projections_s proj = { 0 };

	struct parsed_option_s options[] = {
#define X(name, fn, ...) { key_##name, { 0 }, false, fn },
		GLOBAL_OPTIONS
		COMMON_OPTIONS
#undef X
		{
			"projections", { .ptr_val = &proj },
			false, parse_projections
		}
	};
	int num_options = ARRAY_SIZE(options);
	struct parsed_option_s *sel_option;

	parser_initialized = yaml_parser_initialize(&parser);
	if (!parser_initialized) {
		IOF_LOG_ERROR("Failed to initialize YAML parser");
		D_GOTO(cleanup, ret = -1);
	}

	fp = fopen(path, "r");
	if (!fp) {
		IOF_LOG_ERROR("Unable to open startup config file: %s", path);
		D_GOTO(cleanup, ret = -errno);
	}

	yaml_parser_set_input_file(&parser, fp);
	document_loaded = yaml_parser_load(&parser, &document);
	if (!document_loaded) {
		IOF_LOG_ERROR("Invalid startup config file: %s", path);
		D_GOTO(cleanup, ret = -1);
	}

	ret = parse_node(&document,
			 yaml_document_get_root_node(&document),
			 options, num_options);
	if (ret)
		D_GOTO(cleanup, ret);

	base->projection_count = proj.num_projections;
	if (base->projection_count < 1) {
		IOF_LOG_ERROR("Expected at least one directory in the"
			      " configuration file to be projected");
		D_GOTO(cleanup, ret = -1);
	}

	D_ALLOC_ARRAY(base->projection_array, base->projection_count);
	if (!base->projection_array)
		D_GOTO(cleanup, ret = -1);

#define X(name, fn, ...)						\
	{								\
		sel_option = find_option(options, num_options,		\
					 key_##name,			\
					 strlen(key_##name));		\
		assert(sel_option != NULL);				\
		memcpy(&base->name, sel_option->is_set ?		\
		       &sel_option->buf : (void *)&default_##name,	\
		       sizeof(base->name));				\
	}
	GLOBAL_OPTIONS
#undef X

	for (i = 0; i < base->projection_count; i++) {
#define X(name, fn, ...)						\
	{								\
		sel_option = find_option(proj.options[i],		\
					 proj.num_options,		\
					 key_##name,			\
					 strlen(key_##name));		\
		assert(sel_option != NULL);				\
		if (!sel_option->is_set) {				\
			sel_option = find_option(options, num_options,	\
						 key_##name,		\
						 strlen(key_##name));	\
			assert(sel_option != NULL);			\
		}							\
		memcpy(&base->projection_array[i].name,			\
		       sel_option->is_set ? &sel_option->buf		\
				: (void *)&default_##name,		\
		       sizeof(base->projection_array[i].name));		\
	}
		COMMON_OPTIONS
#undef X

#define X(name, fn, optional, ...)					\
	{								\
		sel_option = find_option(proj.options[i],		\
					 proj.num_options,		\
					 key_##name,			\
					 strlen(key_##name));		\
		assert(sel_option != NULL);				\
		if (!optional && !sel_option->is_set) {			\
			IOF_LOG_ERROR("%s must be set", key_##name);	\
			D_GOTO(cleanup, ret = -1);			\
		}							\
		memcpy(&base->projection_array[i].name,			\
		       &sel_option->buf,				\
		       sizeof(base->projection_array[i].name));		\
	}
		PROJ_OPTIONS
#undef X
	}

cleanup:
	if (proj.options) {
		D_FREE(proj.options[0]);
		D_FREE(proj.options);
	}
	if (document_loaded)
		yaml_document_delete(&document);
	if (parser_initialized)
		yaml_parser_delete(&parser);
	if (fp != NULL)
		fclose(fp);
	return ret;
}

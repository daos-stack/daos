/*
 * (C) Copyright 2017-2023 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __GURT_PARSER_H__
#define __GURT_PARSER_H__

#include <gurt/common.h>
#include <gurt/types.h>
#include <gurt/list.h>

#define D_PARSER_ID_MAX_LEN 64

typedef struct {
	void (*pc_parser_fini_cb)(const char *id, void *arg);
	void (*pc_parser_run_cb)(struct d_string_buffer_t *output, char *buf, int len, void *arg);
} d_parser_cbs_t;

typedef struct {
	d_list_t       ph_link;
	void          *ph_arg;
	d_parser_cbs_t ph_cbs;
	char           ph_id[D_PARSER_ID_MAX_LEN];
} d_parser_handler_t;

typedef struct d_parser {
	d_list_t                 p_handlers;
	struct d_string_buffer_t p_output;
	uint64_t                 p_magic;
} d_parser_t;

/** Initialize a simple parser
 *
 * \param[in,out] parser Handle for parser
 *
 * \return 0 on success of appropriate error code
 */
int
d_parser_init(d_parser_t **parser);

/** Finalize a parser
 *
 * \param[in] parser Handle for parser to finalize
 */
void
d_parser_fini(d_parser_t *parser);

/** Register a parser handler
 *
 * \param[in] parser  Handle to a parser
 * \param[in] id      Unique string identifying the handler
 * \param[in] handler parser handler to register
 * \param[in] arg     Optional argument to pass to callbacks
 *
 * \return 0 on success or appropriate error code
 */
int
d_parser_handler_register(d_parser_t *parser, const char *id, d_parser_cbs_t *cbs, void *arg);

/** Finalize a parser
 *
 * \param[in] Handle for parser to finalize
 */
void
d_parser_fini(d_parser_t *parser);

/** Retrieve the output from the last parsing action
 *
 * \param[in] Handle for the parser
 *
 * \return A null terminated string.  The string will be valid until the next
 * parser action or finalization.
 */
const char *
d_parser_output_get(d_parser_t *parser);

/**
 * The user of d_parser has data they pass in but it's not necessarily
 * in a usable format.  The function d_parser_run will allocate an
 * appropriately sized buffer and pass it to the copy_cb with the data which
 * the callback must fill up to len bytes
 */
typedef int (*d_parser_copy_cb)(char *buf, int len, void *config);

/**
 * Run the parser
 *
 * \param[in] data	Opaque pointer representing the original data to parse
 * \param[in] len	Length of actual data
 * \param[in] copy_cb	Function to fill a supplied buffer with the original
 *                      data
 *
 * \return 0 on success or appropriate error code
 */
int
d_parser_run(d_parser_t *parser, void *data, int len, d_parser_copy_cb copy_cb);

/**
 * Simple helper function to strip whitespace from a string
 *
 * \param[in,out] str A modifiable string
 * \param[in,out] len A length to be modified
 *
 * \return First non-whitespace character in the string
 */
char *
d_strip(char *str, int *len);

/** @}
 */
#endif /* __GURT_PARSER_H__ */

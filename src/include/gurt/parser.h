/*
 * (C) Copyright 2017-2023 Intel Corporation.
 * (C) Copyright 2025 Google LLC
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __GURT_PARSER_H__
#define __GURT_PARSER_H__

#include <gurt/types.h>
#include <gurt/list.h>

#define D_PARSER_ID_MAX_LEN 64

typedef void (*d_parser_run_cb_t)(d_parser_t *output, char *buf, int len, void *arg);

typedef struct d_parser {
	d_list_t                 p_handlers;
	uint64_t                 p_magic;
	char                     p_internal[0];
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
 * \param[in] run_cb  Handler for running the parser
 *
 * \return 0 on success or appropriate error code
 */
int
d_parser_handler_register(d_parser_t *parser, const char *id, d_parser_run_cb_t run_cb);

/** Finalize a parser
 *
 * \param[in] Handle for parser to finalize
 */
void
d_parser_fini(d_parser_t *parser);

/** Retrieve the output from the last parsing action
 *
 * \param[in]  parser Handle for the parser
 * \param[out] len    Length of string
 *
 * \return A null terminated string.  The string will be valid until the next
 * parser action or finalization.
 */
const char *
d_parser_output_get(d_parser_t *parser, int *len);

/**
 * Append the parser output stream.  The stream is reset every time
 * d_parser_run is called.
 *
 * \param[in] parser The parser handle
 * \param[in] fmt    Format string
 */
void
d_parser_output_put(parser_t *parser, const char *fmt, ...)
    __attribute__((__format__(__printf__, 2, 3)));

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
 * \param[in] arg	Argument to pass to parser
 *
 * \return 0 on success or appropriate error code
 */
int
d_parser_run(d_parser_t *parser, void *data, int len, d_parser_copy_cb copy_cb, void *arg);

/**
 * Return a copy of input string that remains valid until
 *
 * \param[in] parser A valid parser handle
 * \param[in] str  A string to copy and strip
 * \param[out] len Length of output string
 * \param[in] strip Strip whitespace from string before returning
 *
 * \return A string that will be valid until next parser operation
 */
char *
d_parser_string_copy(d_parser_t *parser, const char *str, int *len, bool strip);

/** @}
 */
#endif /* __GURT_PARSER_H__ */

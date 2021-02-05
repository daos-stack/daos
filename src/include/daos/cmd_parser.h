/**
 * (C) Copyright 2015-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#ifndef __CMD_PARSER_H__
#define __CMD_PARSER_H__

#include <getopt.h>

/** Read a command line from stdin. */
char *dts_readline(const char *prompt);

/** release a line buffer returned by dts_readline */
void  dts_freeline(char *line);

/**
 * Readline a command line from stdin, parse and execute it.
 *
 * \param [IN]	opts		valid operations
 * \param [IN]	prompt		prompt string
 * \param [IN]	cmd_func	command functions
 */
int dts_cmd_parser(struct option *opts, const char *prompt,
		   int (*cmd_func)(char opc, char *args));

#endif /* __CMD_PARSER_H__ */

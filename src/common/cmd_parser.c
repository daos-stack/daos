/**
 * (C) Copyright 2021-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <daos/common.h>
#include <daos/object.h>
#include <daos/cmd_parser.h>
#include <daos.h>

#define DTS_LINE_SIZE	1024

/**
 * Release a line buffer returned by readline
 */
static void
freeline(char *line)
{
	D_FREE(line);
}

/**
 * Read a command line from stdin, save trouble if we don't have libreadline
 */
static char *
readline(const char *prompt)
{
	char	*line;
	char	*cur;
	bool	 eof;

	D_ALLOC(line, DTS_LINE_SIZE);
	if (!line)
		return NULL;

	if (prompt) {
		fprintf(stdout, "%s", prompt);
		fflush(stdout);
	}

	cur = line;
	eof = false;
	while (1) {
		int	c;

		c = fgetc(stdin);
		if (c == EOF) {
			if (ferror(stdin) || feof(stdin))
				goto out_free;

			eof = true;
			break;
		}

		if (c == '\n')
			break;

		*cur++ = (char)c;
		if (cur - line >= DTS_LINE_SIZE) {
			fprintf(stderr, "line is too long\n");
			goto out_free;
		}
	}
	*cur = '\0';
	if (eof && strlen(line) == 0)
		goto out_free;

	return line;
 out_free:
	freeline(line);
	return NULL;
}

int
cmd_parser(struct option *opts, const char *prompt,
	   int (*cmd_func)(char opc, char *args))
{
	char	*line = NULL;
	int	 rc;

	for (rc = 0; rc == 0;) {
		char *args = NULL;
		char *cmd;
		char  opc;
		int   i;
		bool  eoo = false;

		if (line)
			freeline(line);

		line = readline(prompt);
		if (!line)
			break;

		if (strlen(line) == 0)
			continue; /* empty line */

		cmd = daos_str_trimwhite(line);

		for (i = 0, opc = 0;; i++) {
			struct option *opt;

			opt = &opts[i];
			if (opt->name == NULL) {
				eoo = true;
				break;
			}

			if (strncasecmp(opt->name, cmd, strlen(opt->name)))
				continue;

			/* matched a command */
			opc = (char)opt->val;
			if (opt->has_arg) {
				args = line + strlen(opt->name);
				args = daos_str_trimwhite(args);
			} else {
				args = NULL;
			}
			break;
		}

		if (eoo) {
			D_PRINT("Unknown command string %s, try \"help\"\n", cmd);
			continue;
		}

		rc = cmd_func(opc, args);
		if (rc != 0)
			break;
	}

	if (line)
		freeline(line);

	return rc;
}

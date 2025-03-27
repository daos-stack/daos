/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Original source from h5tools_utils.c */

#include "getopt.h"

#include <stdio.h>
#include <string.h>

int opt_ind_g = 1;            /* token pointer */
const char *opt_arg_g = NULL; /* flag argument (or value) */

int
getopt(int argc, char *argv[], const char *opts, const struct option *l_opts)
{
    static int sp = 1; /* character index in current token */
    int opt_opt = '?'; /* option character passed back to user */

    if (sp == 1) {
        /* check for more flag-like tokens */
        if (opt_ind_g >= argc || argv[opt_ind_g][0] != '-' ||
            argv[opt_ind_g][1] == '\0') {
            return EOF;
        } else if (strcmp(argv[opt_ind_g], "--") == 0) {
            opt_ind_g++;
            return EOF;
        }
    }

    if (sp == 1 && argv[opt_ind_g][0] == '-' && argv[opt_ind_g][1] == '-') {
        /* long command line option */
        const char *arg = &argv[opt_ind_g][2];
        int i;

        for (i = 0; l_opts && l_opts[i].name; i++) {
            size_t len = strlen(l_opts[i].name);

            if (strncmp(arg, l_opts[i].name, len) == 0) {
                /* we've found a matching long command line flag */
                opt_opt = l_opts[i].shortval;

                if (l_opts[i].has_arg != no_arg) {
                    if (arg[len] == '=') {
                        opt_arg_g = &arg[len + 1];
                    } else if (l_opts[i].has_arg != optional_arg) {
                        if (opt_ind_g < (argc - 1))
                            if (argv[opt_ind_g + 1][0] != '-')
                                opt_arg_g = argv[++opt_ind_g];
                    } else if (l_opts[i].has_arg == require_arg) {
                        fprintf(stderr,
                            "%s: option required for \"--%s\" flag\n", argv[0],
                            arg);
                        opt_opt = '?';
                    } else
                        opt_arg_g = NULL;
                } else {
                    if (arg[len] == '=') {
                        fprintf(stderr,
                            "%s: no option required for \"%s\" flag\n", argv[0],
                            arg);
                        opt_opt = '?';
                    }
                    opt_arg_g = NULL;
                }
                break;
            }
        }

        if (l_opts && l_opts[i].name == NULL) {
            /* exhausted all of the l_opts we have and still didn't match */
            fprintf(stderr, "%s: unknown option \"%s\"\n", argv[0], arg);
            opt_opt = '?';
        }
        opt_ind_g++;
        sp = 1;
    } else {
        char *cp; /* pointer into current token */

        /* short command line option */
        opt_opt = argv[opt_ind_g][sp];

        if (opt_opt == ':' || (cp = strchr(opts, opt_opt)) == 0) {
            fprintf(stderr, "%s: unknown option \"%c\"\n", argv[0], opt_opt);

            /* if no chars left in this token, move to next token */
            if (argv[opt_ind_g][++sp] == '\0') {
                opt_ind_g++;
                sp = 1;
            }
            return '?';
        }
        if (*++cp == ':') {
            /* if a value is expected, get it */
            if (argv[opt_ind_g][sp + 1] != '\0') {
                /* flag value is rest of current token */
                opt_arg_g = &argv[opt_ind_g++][sp + 1];
            } else if (++opt_ind_g >= argc) {
                fprintf(stderr, "%s: value expected for option \"%c\"\n",
                    argv[0], opt_opt);
                opt_opt = '?';
            } else {
                /* flag value is next token */
                opt_arg_g = argv[opt_ind_g++];
            }
            sp = 1;
        }
        /* wildcard argument */
        else if (*cp == '*') {
            /* check the next argument */
            opt_ind_g++;
            /* we do have an extra argument, check if not last */
            if (argv[opt_ind_g][0] != '-' && (opt_ind_g + 1) < argc) {
                opt_arg_g = argv[opt_ind_g++];
            } else {
                opt_arg_g = NULL;
            }
        } else {
            /* set up to look at next char in token, next time */
            if (argv[opt_ind_g][++sp] == '\0') {
                /* no more in current token, so setup next token */
                opt_ind_g++;
                sp = 1;
            }
            opt_arg_g = NULL;
        }
    }

    /* return the current flag character found */
    return opt_opt;
}

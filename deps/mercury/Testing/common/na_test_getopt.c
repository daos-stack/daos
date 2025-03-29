/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Original source from h5tools_utils.c */

#include "na_test_getopt.h"

#include <stdio.h>
#include <string.h>

int na_test_opt_ind_g = 1;            /* token pointer */
const char *na_test_opt_arg_g = NULL; /* flag argument (or value) */
const char *na_test_short_opt_g =
    "hc:d:p:H:P:sSk:l:bC:X:VZ:y:z:w:x:mt:BRvMUf:T:u:i:";
/* clang-format off */
const struct na_test_opt na_test_opt_g[] = {
    {"help", no_arg, 'h'},
    {"comm", require_arg, 'c'},
    {"domain", require_arg, 'd'},
    {"protocol", require_arg, 'p'},
    {"hostname", require_arg, 'H'},
    {"port", require_arg, 'P'},
    {"mpi_static", no_arg, 's'},
    {"self_send", no_arg, 'S'},
    {"key", require_arg, 'k'},
    {"loop", require_arg, 'l'},
    {"busy", no_arg, 'b'},
    {"classes", require_arg, 'C'},
    {"contexts", require_arg, 'X'},
    {"verbose", no_arg, 'V'},
    {"msg_size", require_arg, 'Z'},
    {"buf_size_min", require_arg, 'y'},
    {"buf_size_max", require_arg, 'z'},
    {"buf_count", require_arg, 'w'},
    {"handle", require_arg, 'x'},
    {"memory", no_arg, 'm'},
    {"threads", require_arg, 't'},
    {"bidirectional", no_arg, 'B'},
    {"force-register", no_arg, 'R'},
    {"verify", no_arg, 'v'},
    {"millionbps", no_arg, 'M'},
    {"no-multi-recv", no_arg, 'U'},
    {"hostfile", require_arg, 'f'},
    {"tclass", require_arg, 'T'},
    {"mrecv-ops", require_arg, 'u'},
    {"post-init", require_arg, 'i'},
    {NULL, 0, '\0'} /* Must add this at the end */
};
/* clang-format on */

int
na_test_getopt(
    int argc, char *argv[], const char *opts, const struct na_test_opt *l_opts)
{
    static int sp = 1; /* character index in current token */
    int opt_opt = '?'; /* option character passed back to user */

    if (sp == 1) {
        /* check for more flag-like tokens */
        if (na_test_opt_ind_g >= argc || argv[na_test_opt_ind_g][0] != '-' ||
            argv[na_test_opt_ind_g][1] == '\0') {
            return EOF;
        } else if (strcmp(argv[na_test_opt_ind_g], "--") == 0) {
            na_test_opt_ind_g++;
            return EOF;
        }
    }

    if (sp == 1 && argv[na_test_opt_ind_g][0] == '-' &&
        argv[na_test_opt_ind_g][1] == '-') {
        /* long command line option */
        const char *arg = &argv[na_test_opt_ind_g][2];
        int i;

        for (i = 0; l_opts && l_opts[i].name; i++) {
            size_t len = strlen(l_opts[i].name);

            if (strncmp(arg, l_opts[i].name, len) == 0) {
                /* we've found a matching long command line flag */
                opt_opt = l_opts[i].shortval;

                if (l_opts[i].has_arg != no_arg) {
                    if (arg[len] == '=') {
                        na_test_opt_arg_g = &arg[len + 1];
                    } else if (l_opts[i].has_arg != optional_arg) {
                        if (na_test_opt_ind_g < (argc - 1))
                            if (argv[na_test_opt_ind_g + 1][0] != '-')
                                na_test_opt_arg_g = argv[++na_test_opt_ind_g];
                    } else if (l_opts[i].has_arg == require_arg) {
                        fprintf(stderr,
                            "%s: option required for \"--%s\" flag\n", argv[0],
                            arg);
                        opt_opt = '?';
                    } else
                        na_test_opt_arg_g = NULL;
                } else {
                    if (arg[len] == '=') {
                        fprintf(stderr,
                            "%s: no option required for \"%s\" flag\n", argv[0],
                            arg);
                        opt_opt = '?';
                    }
                    na_test_opt_arg_g = NULL;
                }
                break;
            }
        }

        if (l_opts && l_opts[i].name == NULL) {
            /* exhausted all of the l_opts we have and still didn't match */
            fprintf(stderr, "%s: unknown option \"%s\"\n", argv[0], arg);
            opt_opt = '?';
        }
        na_test_opt_ind_g++;
        sp = 1;
    } else {
        char *cp; /* pointer into current token */

        /* short command line option */
        opt_opt = argv[na_test_opt_ind_g][sp];

        if (opt_opt == ':' || (cp = strchr(opts, opt_opt)) == 0) {
            fprintf(stderr, "%s: unknown option \"%c\"\n", argv[0], opt_opt);

            /* if no chars left in this token, move to next token */
            if (argv[na_test_opt_ind_g][++sp] == '\0') {
                na_test_opt_ind_g++;
                sp = 1;
            }
            return '?';
        }
        if (*++cp == ':') {
            /* if a value is expected, get it */
            if (argv[na_test_opt_ind_g][sp + 1] != '\0') {
                /* flag value is rest of current token */
                na_test_opt_arg_g = &argv[na_test_opt_ind_g++][sp + 1];
            } else if (++na_test_opt_ind_g >= argc) {
                fprintf(stderr, "%s: value expected for option \"%c\"\n",
                    argv[0], opt_opt);
                opt_opt = '?';
            } else {
                /* flag value is next token */
                na_test_opt_arg_g = argv[na_test_opt_ind_g++];
            }
            sp = 1;
        }
        /* wildcard argument */
        else if (*cp == '*') {
            /* check the next argument */
            na_test_opt_ind_g++;
            /* we do have an extra argument, check if not last */
            if (argv[na_test_opt_ind_g][0] != '-' &&
                (na_test_opt_ind_g + 1) < argc) {
                na_test_opt_arg_g = argv[na_test_opt_ind_g++];
            } else {
                na_test_opt_arg_g = NULL;
            }
        } else {
            /* set up to look at next char in token, next time */
            if (argv[na_test_opt_ind_g][++sp] == '\0') {
                /* no more in current token, so setup next token */
                na_test_opt_ind_g++;
                sp = 1;
            }
            na_test_opt_arg_g = NULL;
        }
    }

    /* return the current flag character found */
    return opt_opt;
}

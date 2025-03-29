/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* Original source from h5tools_utils.h */

#ifndef GETOPT_H
#define GETOPT_H

/*
 * get_option determines which options are specified on the command line and
 * returns a pointer to any arguments possibly associated with the option in
 * the ``opt_arg'' variable. get_option returns the shortname equivalent of
 * the option. The long options are specified in the following way:
 *
 * struct long_options foo[] = {
 *   { "filename", require_arg, 'f' },
 *   { "append", no_arg, 'a' },
 *   { "width", require_arg, 'w' },
 *   { NULL, 0, 0 }
 * };
 *
 * Long named options can have arguments specified as either:
 *
 *   ``--param=arg'' or ``--param arg''
 *
 * Short named options can have arguments specified as either:
 *
 *   ``-w80'' or ``-w 80''
 *
 * and can have more than one short named option specified at one time:
 *
 *   -aw80
 *
 * in which case those options which expect an argument need to come at the
 * end.
 */
struct option {
    const char *name; /* name of the long option                */
    int has_arg;      /* whether we should look for an arg      */
    char shortval;    /* the shortname equivalent of long arg
                       * this gets returned from na_test_getopt */
};

enum {
    no_arg = 0,  /* doesn't take an argument */
    require_arg, /* requires an argument     */
    optional_arg /* argument is optional     */
};

extern int opt_ind_g;         /* token pointer */
extern const char *opt_arg_g; /* flag argument (or value) */

#ifdef __cplusplus
extern "C" {
#endif

int
getopt(int argc, char *argv[], const char *opts, const struct option *l_opts);

#ifdef __cplusplus
}
#endif

#endif /* GETOPT_H */

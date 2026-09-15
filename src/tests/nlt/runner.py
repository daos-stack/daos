"""NLT: test selection and top-level orchestration.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import subprocess  # nosec
import sys
import time

from .base import BoolRatchet, NLTestFail
from .client import run_daos_cmd
from .config import check_memcheck_build, load_conf
from .dfuse import needs_dfuse_with_opt
from .fault_injection import (test_alloc_cont_create, test_alloc_fail, test_alloc_fail_cat,
                              test_alloc_fail_cont_create, test_alloc_fail_copy,
                              test_alloc_fail_copy_trunc, test_alloc_fail_il_cp,
                              test_alloc_pil4dfs_ls, test_dfs_check, test_dfuse_start,
                              test_fi_cont_check, test_fi_cont_query, test_fi_get_attr,
                              test_fi_get_prop, test_fi_list_attr)
from .logging_utils import close_log_test, setup_log_test
from .posix_tests import PosixTests, run_posix_tests
from .reporting import WarningsFactory
from .server import DaosServer
from .special_tests import (check_readdir_perf, run_dfuse, run_duns_overlay_test, run_in_fg,
                            test_pydaos_kv, test_pydaos_kv_obj_class)
from .watchdog import WedgeWatch


def generate_special_test_list():
    """List all special tests"""
    return ['special_dfuse_multi', 'special_dfuse_overlay']


def is_special_testname(testname):
    """Check whether a test is special"""
    return testname.startswith('special')


def printable_test_list(raw_test_list):
    """Return a printable list of tests, without the 'test_' prefix,
    and with a * after the name if it can be runw with both caching and
    caching off."""
    test_variants = \
        [(x, needs_dfuse_with_opt.get_test_variants(x)) for x in raw_test_list]
    test_list = [x[5:] if len(y) < 2 else x[5:] + '*' for x, y in test_variants]
    test_list.extend(generate_special_test_list())
    return test_list


def expand_input_list(input_list):
    """Expand the list of user-specified tests; i.e., convert to a dict where
    keys are the test names and the values are the list of variants."""
    input_params = [needs_dfuse_with_opt.parse_test_name(x) for x in input_list]
    name_dict = {}
    for name, parameterization in input_params:
        if name not in name_dict:
            name_dict[name] = []
        param_list = name_dict[name]
        if parameterization is not None and parameterization not in param_list:
            param_list.append(parameterization)

    # Check if user specified an non-parameterized test name (e.g., 'read') and we need to include
    # all of its variants.
    possible_expansions = [(x, needs_dfuse_with_opt.get_test_variants(x))
                           for x in name_dict]
    for name, parameters in possible_expansions:
        if name in name_dict and len(name_dict[name]) == 0:
            name_dict[name] = parameters

    return name_dict


def explicit_list_to_exclusion_list(name_dict):
    """Convert a dict of explicitly requested tests to an exclusion dict of variants not to
    run."""
    test_variants = {x: needs_dfuse_with_opt.get_test_variants(x) for x in name_dict}
    exclusion_dict = {}
    for name in name_dict.keys():
        exclusion_list = [x for x in test_variants[name] if x not in name_dict[name]]
        if len(exclusion_list) > 0:
            exclusion_dict[name] = exclusion_list
    return exclusion_dict


def expand_test_list(raw_test_list, excluded_name_dict):
    """Expand a test list into a dict where the keys are test names and the values are a list of
    variants of that test (if any). Remove any test names where all variants have
    been excluded."""
    test_variants = {x: needs_dfuse_with_opt.get_test_variants(x) for x in raw_test_list}

    keys_to_remove = []
    keys_to_update = []
    for key, vals in test_variants.items():
        if len(vals) > 0:
            viable = [x for x in vals if x not in excluded_name_dict[key]] \
                if key in excluded_name_dict else list(vals)
            if len(viable) == 0:
                keys_to_remove.append(key)
            else:
                keys_to_update.append((key, viable))

    for key in keys_to_remove:
        del test_variants[key]
    test_variants.update(keys_to_update)

    return test_variants


def _run_test_pass(conf, args, server, fatal_errors, special_list, test_dict, excluded_dict):
    """Run one pass of the requested tests against server; return whether FI/dfuse is wanted."""
    fi_test_dfuse = False
    if args.mode == 'launch':
        run_in_fg(server, conf, args)
    elif args.mode == 'overlay' and 'special_dfuse_overlay' in special_list:
        fatal_errors.add_result(run_duns_overlay_test(server, conf))
    elif args.mode == 'set-fi':
        fatal_errors.add_result(server.set_fi())
    elif args.mode == 'all':
        fi_test_dfuse = True
        fatal_errors.add_result(run_posix_tests(server, conf, test_dict.keys()))
        if 'special_dfuse_multi' in special_list:
            fatal_errors.add_result(run_dfuse(server, conf))
        if 'special_dfuse_overlay' in special_list:
            fatal_errors.add_result(run_duns_overlay_test(server, conf))
        test_pydaos_kv(server, conf)
        test_pydaos_kv_obj_class(server, conf)
        fatal_errors.add_result(server.set_fi())
    elif args.test == 'all':
        fatal_errors.add_result(run_posix_tests(server, conf, test_dict.keys()))
    elif args.test:
        special_list = [x for x in args.test if is_special_testname(x)]
        despecialed_list = ['test_' + x for x in args.test if not is_special_testname(x)]
        custom_test_dict = expand_input_list(despecialed_list)
        custom_exclusions = explicit_list_to_exclusion_list(custom_test_dict)
        exclusion_union = {}
        for key in custom_test_dict:
            exclusion_list = \
                list(set(custom_exclusions.get(key, [])).union(
                    set(excluded_dict.get(key, []))))
            if len(exclusion_list) > 0:
                exclusion_union[key] = exclusion_list
        needs_dfuse_with_opt.record_exclusions(exclusion_union)
        custom_filtered_dict = expand_test_list(custom_test_dict.keys(), exclusion_union)
        if len(custom_filtered_dict) == 0 and len(special_list) == 0:
            print('No tests to run!')
            sys.exit(1)
        if len(custom_filtered_dict) > 0:
            fatal_errors.add_result(
                run_posix_tests(server, conf, custom_filtered_dict.keys()))
        if 'special_dfuse_multi' in special_list:
            fatal_errors.add_result(run_dfuse(server, conf))
        if 'special_dfuse_overlay' in special_list:
            fatal_errors.add_result(run_duns_overlay_test(server, conf))
    else:
        fatal_errors.add_result(run_posix_tests(server, conf, test_dict.keys()))
        if 'special_dfuse_multi' in special_list:
            fatal_errors.add_result(run_dfuse(server, conf))
        fatal_errors.add_result(server.set_fi())
    return fi_test_dfuse


def run(wf, args):
    """Main entry point"""
    # pylint: disable=too-many-branches

    run_start = time.perf_counter()

    posix_exclusions = [x for x in args.exclude_test if not is_special_testname(x)] \
        if args.exclude_test else []
    special_exclusions = [x for x in args.exclude_test if is_special_testname(x)] \
        if args.exclude_test else []
    special_list = [x for x in generate_special_test_list() if x not in special_exclusions]
    excluded_dict = expand_input_list(['test_' + x for x in posix_exclusions])
    needs_dfuse_with_opt.record_exclusions(excluded_dict)
    if args.suite == 'manual':
        raw_test_list = PosixTests.generate_manual_test_list()
    elif args.suite == 'all':
        raw_test_list = PosixTests.generate_test_list() + PosixTests.generate_manual_test_list()
    elif args.suite == 'ci':
        raw_test_list = PosixTests.generate_test_list()
    else:
        raise NLTestFail(f'Unknown suite: {args.suite}')
    test_dict = expand_test_list(raw_test_list, excluded_dict)
    if len(test_dict) == 0:
        print('No tests to run!')
        sys.exit(1)

    conf = load_conf(args)

    wf_server = WarningsFactory('nlt-server-leaks.json', post=True, check='Server leak checking')
    wf.link(wf_server)

    conf.set_wf(wf)
    conf.set_args(args)
    if args.memcheck != 'no':
        check_memcheck_build(conf)
    setup_log_test(conf)

    # Arm the summary now so it is still emitted if a test, startup or teardown raises.
    wf.arm_summary(args.summary, args, conf, run_start)

    def _report_wedge(failure, output):
        wf.add_test_case('wedge_watchdog', failure, output=output)

    WedgeWatch(args.failfast, log_dir=conf.tmp_dir, report=_report_wedge,
               finalize=wf.write_summary).start()

    fi_test = False
    fi_test_dfuse = False

    fatal_errors = BoolRatchet()

    if args.mode == 'fi':
        fi_test = True
    else:
        for rep in range(args.repeat):
            if args.repeat > 1:
                print(f'=== NLT repeat iteration {rep + 1}/{args.repeat} ===')

            try:
                # reset after each iteration, except on the last one
                with DaosServer(conf, test_class='first', wf=wf_server,
                                fatal_errors=fatal_errors,
                                wipe_on_exit=rep < args.repeat - 1) as server:
                    fi_test_dfuse = _run_test_pass(conf, args, server, fatal_errors,
                                                   special_list, test_dict, excluded_dict)
            except Exception as error:  # pylint: disable=broad-exception-caught
                if args.repeat == 1:
                    raise
                if args.failfast:
                    # re-raise so the traceback is preserved for debugging
                    print(f'--failfast set; stopping after iteration {rep + 1}/{args.repeat}')
                    raise
                print(f'NLT repeat iteration {rep + 1} raised: {error}')
                fatal_errors.add_result(True)
            if args.failfast and fatal_errors.errors and rep < args.repeat - 1:
                print(f'--failfast set; stopping after iteration {rep + 1}/{args.repeat}')
                break

    if args.mode == 'all':
        with DaosServer(conf, test_class='restart', wf=wf_server,
                        fatal_errors=fatal_errors) as server:
            pass

    # If running all tests then restart the server under valgrind.
    # This is really, really slow so just do cont list, then
    # exit again.
    if args.server_valgrind:
        with DaosServer(conf, test_class='valgrind', wf=wf_server, valgrind=True,
                        fatal_errors=fatal_errors) as server:
            pools = server.fetch_pools()
            for pool in pools:
                cmd = ['pool', 'query', pool.id()]
                rc = run_daos_cmd(conf, cmd, valgrind=False)
                print(rc)
                time.sleep(5)
                cmd = ['cont', 'list', pool.id()]
                run_daos_cmd(conf, cmd, valgrind=False)
            time.sleep(20)

    # If the perf-check option is given then re-start everything without much
    # debugging enabled and run some micro-benchmarks to give numbers for use
    # as a comparison against other builds.
    run_fi = False

    if args.perf_check or fi_test or fi_test_dfuse:
        fi_env = os.environ.copy()
        fi_env['PATH'] = f'{conf["PREFIX"]}/bin:{fi_env["PATH"]}'
        fs = subprocess.run(['fault_status'], check=False, env=fi_env)
        print(fs)
        if fs.returncode == 0:
            run_fi = True
        elif fi_test or fi_test_dfuse:
            raise NLTestFail('Unable to detect fault injection feature '
                             '- cannot run requested FI tests')
        else:
            print("Unable to detect fault injection feature - skipping FI testing")

    if run_fi:
        args.server_debug = 'INFO'
        args.memcheck = 'no'
        args.dfuse_debug = 'WARN'
        with DaosServer(conf, test_class='no-debug', wf=wf_server,
                        fatal_errors=fatal_errors) as server:
            if fi_test:
                # Most of the fault injection tests go here, they are then run on docker containers
                # so can be performed in parallel.

                wf_client = WarningsFactory('nlt-client-leaks.json')
                wf.link(wf_client)

                # dfuse start-up, uses custom fault to force exit if no other faults injected.
                fatal_errors.add_result(test_dfuse_start(server, conf, wf_client))

                # list-container test.
                fatal_errors.add_result(test_alloc_fail(server, conf))

                # Container query test.
                fatal_errors.add_result(test_fi_cont_query(server, conf, wf_client))

                fatal_errors.add_result(test_fi_cont_check(server, conf, wf_client))

                # Container attribute tests
                fatal_errors.add_result(test_fi_get_attr(server, conf, wf_client))
                fatal_errors.add_result(test_fi_list_attr(server, conf, wf_client))

                fatal_errors.add_result(test_fi_get_prop(server, conf, wf_client))

                # filesystem copy tests.
                fatal_errors.add_result(test_alloc_fail_copy(server, conf, wf_client))
                fatal_errors.add_result(test_alloc_fail_copy_trunc(server, conf, wf_client))

                # container create with properties test.
                fatal_errors.add_result(test_alloc_cont_create(server, conf, wf_client))

                # Long/opt-in fault-injection tests. test_dfs_check takes ~4 hours and can fill
                # available disk, and test_alloc_pil4dfs_ls is currently unreliable, so these only
                # run when the manual suite is explicitly requested (--suite manual|all).
                if args.suite in ('manual', 'all'):
                    fatal_errors.add_result(test_alloc_pil4dfs_ls(server, conf, wf_client))
                    fatal_errors.add_result(test_dfs_check(server, conf, wf_client))

                wf_client.close()

            if fi_test_dfuse:
                # We cannot yet run dfuse inside docker containers and some of the failure modes
                # aren't well handled so continue to run the dfuse fault injection test on real
                # hardware.

                fatal_errors.add_result(test_alloc_fail_cont_create(server, conf))

                # Read-via-IL test, requires dfuse.
                fatal_errors.add_result(test_alloc_fail_cat(server, conf))

                # Copy (read/write) via IL, requires dfuse.
                fatal_errors.add_result(test_alloc_fail_il_cp(server, conf))

            if args.perf_check:
                check_readdir_perf(server, conf)

    if fatal_errors.errors:
        wf.add_test_case('Errors', 'Significant errors encountered')
    else:
        wf.add_test_case('Errors')

    if conf.valgrind_errors:
        wf.add_test_case('Errors', 'Valgrind errors encountered')
        print("Valgrind errors detected during execution")

    wf_server.close()
    close_log_test(conf)
    conf.cleanup()
    if args.summary:
        wf.write_summary()
    print(f'Total time in log analysis: {conf.log_timer.total:.2f} seconds')
    print(f'Total time in log compression: {conf.compress_timer.total:.2f} seconds')
    return fatal_errors

#!/usr/bin/env python3
# Copyright (C) 2016-2019 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""
Memory allocation failure testing for IOF.

Intercept memory allocations in CaRT to cause them to fail, and run the program
to see how it behaves.  For each allocation point as the program runs fail
and see if the program exits cleanly and shuts down properly without leaking
any memory.

This test targets the startup procedure of both the CNSS and the IONSS.
"""

import os
import sys
import yaml
import tempfile
import subprocess
import iof_cart_logparse
import iof_cart_logtest
import iofcommontestsuite
import rpctrace_common_methods


FAIL_ON_ERROR = False

def unlink_file(file_name):
    """Unlink a file without failing if it doesn't exist"""

    try:
        os.unlink(file_name)
    except FileNotFoundError:
        pass

class EndOfTest(Exception):
    """Raised when no injected fault is found"""
    pass

#pylint: disable=too-many-locals
#pylint: disable=too-many-statements
def run_once(prefix, cmd, log_top_dir, floc):
    """Run a single instance of the command"""
    print("Testing {}".format(floc))

    # There is a bug here that if the test runs but does not inject a fault
    # because "floc" is too high but it does find errors then the upper
    # loop does not exit, just keeps calling this function with higher
    # values of floc.
    #
    # One option would be to run as a first instance without
    # a value for floc to check the code is clean on the normal case before
    # doing fault injection which might give faster failure for easy bugs
    # however it wouldn't well handle the case where errors were the result
    # of race conditions.

    log_file = os.path.join(log_top_dir, 'af', 'fail_{}.log'.format(floc))
    internals_file = os.path.join(log_top_dir, 'af',
                                  'fail_{}.internals.log'.format(floc))
    unlink_file(log_file)

    y = {}
    # Fault id 100 is in ionss itself, and is used to trigger shutdown
    # when IONSS has finished initialising, so this test can complete
    # rather than block.
    y['fault_config'] = [{'id': 0,
                          'interval': floc,
                          'max_faults': 1},
                         {'id': 100}]

    fd = open(os.path.join(prefix, 'fi.yaml'), 'w+')
    fd.write(yaml.dump(y))
    fd.close()
    os.environ['D_FI_CONFIG'] = os.path.join(prefix, 'fi.yaml')
    os.environ['D_LOG_FILE'] = log_file
    os.environ['CRT_PHY_ADDR_STR'] = 'ofi+sockets'
    os.environ['OFI_INTERFACE'] = 'lo'
    rc = subprocess.call(cmd)
    print('Return code was {}'.format(rc))
    # This is a valgrind error code and means memory leaks
    if rc == 42:
        for line in iof_cart_logparse.IofLogIter(log_file):
            if not line.endswith('fault_id 0, injecting fault.'):
                continue
            iof_cart_logtest.show_line(line, 'error',
                                       'Valgrind error when fault injected')
            iof_cart_logtest.show_bug(line, 'IOF-887')
        print("Alloc test failing with valgrind errors")
        return True
    # This means abnormal exit, probably a segv.
    # Less than zero means abnormal exit, probably from a signal
    # 0 means no error reported which there should be.
    # > 10 means it's not a CNSS_ERR code.
    if rc <= 0 or rc > 10:
        return True

    ifd = open(internals_file, 'w+')
    li = iof_cart_logparse.IofLogIter(log_file)
    trace = rpctrace_common_methods.RpcTrace(li, ifd)
    trace.rpc_reporting(trace.pids[0])
    trace.descriptor_rpc_trace(trace.pids[0])

    have_inject = False
    have_eot = False

    for line in li:
        if line.endswith('fault_id 100, injecting fault.'):
            print(line.to_str())
            have_eot = True
            break
        if not line.endswith('fault_id 0, injecting fault.'):
            continue
        have_inject = True
        print(line.to_str())

    ifd.close()

    try:
        ct = iof_cart_logtest.LogTest(li)
        ct.set_error_ok('src/common/iof_bulk.c')
        ct.check_log_file(False)

    except iof_cart_logtest.LogCheckError as e:
        print(e)
        print("Log tracing code found errors: {}".format(internals_file))
        return True

    if trace.have_errors:
        print("Internals tracing code found errors: {}".format(internals_file))
        return True

    if not have_inject:
        raise EndOfTest

    # If a fault was injected but ignored then fail the test.
    if have_eot:
        print("Fault was injected but silently ignored")
        return True
    return False
#pylint: enable=too-many-locals

def open_config_file():
    """Write a ionss config file"""

    config = {"projections":
              [{"full_path": '/tmp'}]}

    config_file = tempfile.NamedTemporaryFile(suffix='.cfg',
                                              prefix="ionss_",
                                              mode='w',
                                              delete=False)
    yaml.dump(config, config_file.file, default_flow_style=False)
    config_file.close()
    return config_file.name

#pylint: disable=too-many-branches
#pylint: disable=too-many-statements
def run_app(ionss=False):
    """Main function"""

    jdata = iofcommontestsuite.load_config()
    export_tmp_dir = os.getenv("IOF_TMP_DIR", '/tmp')
    prefix = tempfile.mkdtemp(prefix='iof_allocf_',
                              dir=export_tmp_dir)
    ctrl_fs_dir = os.path.join(prefix, '.ctrl')

    # Hardcode this to on for now to enable valgrind in CI.
    use_valgrind = True

    if ionss:
        bin_cmd = 'ionss'
    else:
        bin_cmd = 'cnss'

    log_top_dir = os.getenv("IOF_TESTLOG",
                            os.path.join(os.path.dirname(
                                os.path.realpath(__file__)),
                                         'output',
                                         'alloc_fail',
                                         bin_cmd))

    try:
        os.makedirs(os.path.join(log_top_dir, 'af'))
    except FileExistsError:
        pass

    crt_suppressfile = iofcommontestsuite.valgrind_cart_supp_file()
    iof_suppressfile = iofcommontestsuite.valgrind_iof_supp_file()

    floc = 0

    if ionss:
        config_file = open_config_file()

    my_res = []
    while True:
        floc += 1
        if not ionss:
            subprocess.call(['fusermount', '-q', '-u', ctrl_fs_dir])
        cmd = [os.path.join(jdata['OMPI_PREFIX'], 'bin', 'orterun'), '-n', '1']
        cmd.extend(['-x', 'D_LOG_MASK=DEBUG'])
        cmd.extend(['valgrind',
                    '--quiet',
                    '--leak-check=full',
                    '--show-leak-kinds=all',
                    '--suppressions={}'.format(iof_suppressfile),
                    '--error-exitcode=42'])
        if crt_suppressfile:
            cmd.append('--suppressions={}'.format(crt_suppressfile))

        # If running in a CI environment then output to xml.
        if use_valgrind:
            cmd.extend(['--xml=yes',
                        '--xml-file={}'.format(
                            os.path.join(log_top_dir, 'af',
                                         "fail_{}.memcheck".format(floc)))])
        if ionss:
            cmd.append(os.path.join(jdata['PREFIX'], 'bin', 'ionss'))
            cmd.extend(['-c', config_file])
        else:
            cmd.append(os.path.join(jdata['PREFIX'], 'bin', 'cnss'))
            cmd.extend(['-p', prefix])
        try:
            res = run_once(prefix, cmd, log_top_dir, floc)
            if res:
                my_res.append(floc)
                if FAIL_ON_ERROR:
                    sys.exit(1)
        except EndOfTest:
            print("Ran without injecting error")
            break
    if ionss:
        os.unlink(config_file)

    if my_res:
        print('Failed for {}'.format(my_res))
        sys.exit(1)
#pylint: enable=too-many-branches
#pylint: enable=too-many-statements

if __name__ == '__main__':
    run_app()
    run_app(ionss=True)

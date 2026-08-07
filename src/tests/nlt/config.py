"""NLT: build configuration and environment helpers.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import json
import os
import subprocess  # nosec
import tempfile
from os.path import join

from .base import CulmTimer, NLTestFail


class NLTConf():
    """Helper class for configuration"""

    def __init__(self, json_file, args):

        with open(json_file, 'r') as ofh:
            self._bc = json.load(ofh)
        self.agent_dir = None
        self.wf = None
        self.args = None
        self.max_log_size = None
        self.valgrind_errors = False
        self.log_timer = CulmTimer()
        self.compress_timer = CulmTimer()
        self.dfuse_parent_dir = tempfile.mkdtemp(dir=args.dfuse_dir,
                                                 prefix='dnt_dfuse_')
        self.tmp_dir = None
        if args.class_name:
            self.tmp_dir = join('nlt_logs', args.class_name)
            if os.path.exists(self.tmp_dir):
                for old_file in os.listdir(self.tmp_dir):
                    os.unlink(join(self.tmp_dir, old_file))
                os.rmdir(self.tmp_dir)
            os.makedirs(self.tmp_dir)

        self._compress_procs = []

    def __del__(self):
        self.flush_bz2()
        os.rmdir(self.dfuse_parent_dir)

    def set_wf(self, wf):
        """Set the WarningsFactory object"""
        self.wf = wf

    def set_args(self, args):
        """Set command line args"""
        self.args = args

        # Parse the max log size.
        if args.max_log_size:
            size = args.max_log_size
            if size.endswith('MiB'):
                size = int(size[:-3])
                size *= (1024 * 1024)
            elif size.endswith('GiB'):
                size = int(size[:-3])
                size *= (1024 * 1024 * 1024)
            self.max_log_size = int(size)

    def __getitem__(self, key):
        return self._bc[key]

    def compress_file(self, filename):
        """Compress a file using bz2 for space reasons

        Launch a bzip2 process in the background as this is time consuming, and each time
        a new process is launched then reap any previous ones which have completed.
        """
        # pylint: disable=consider-using-with
        self._compress_procs[:] = (proc for proc in self._compress_procs if proc.poll() is None)
        self._compress_procs.append(subprocess.Popen(['nice', '-19', 'bzip2', '--best', filename]))

    def flush_bz2(self):
        """Wait for all bzip2 subprocess to finish"""
        self.compress_timer.start()
        for proc in self._compress_procs:
            proc.wait()
        self._compress_procs = []
        self.compress_timer.stop()


def load_conf(args):
    """Load the build config file"""
    file_self = os.path.dirname(os.path.abspath(__file__))
    json_file = None
    while True:
        new_file = join(file_self, '.build_vars.json')
        if os.path.exists(new_file):
            json_file = new_file
            break
        file_self = os.path.dirname(file_self)
        if file_self == '/':
            raise NLTestFail('.build_vars.json file not found')
    return NLTConf(json_file, args)


def get_base_env(clean=False):
    """Return the base set of env vars needed for DAOS"""
    if clean:
        env = {}
    else:
        env = os.environ.copy()
    env['DD_MASK'] = 'all'
    env['DD_SUBSYS'] = 'all'
    env['D_LOG_MASK'] = 'DEBUG'
    env['D_LOG_SIZE'] = '5g'
    env['FI_UNIVERSE_SIZE'] = '128'

    # If set, retain the HTTPS_PROXY for valgrind
    http_proxy = os.environ.get('HTTPS_PROXY')
    if http_proxy:
        env['HTTPS_PROXY'] = http_proxy
    no_proxy = os.environ.get('NO_PROXY')
    if no_proxy:
        env['NO_PROXY'] = no_proxy

    # Enable this to debug memory errors, it has a performance impact but will scan the heap
    # for corruption.  See DAOS-12735 for why this can cause problems in practice.
    # env['MALLOC_CHECK_'] = '3'

    # Otherwise max number of contexts will be limited by number of cores
    env['CRT_CTX_NUM'] = '32'

    return env


def check_memcheck_build(conf):
    """Fail early if the daos binary is not valgrind-tagged for a memcheck run."""
    daos_bin = join(conf['PREFIX'], 'bin', 'daos')
    with open(daos_bin, 'rb') as fd:
        if b'runtime.valgrindRegisterStack' not in fd.read():
            raise NLTestFail(
                f'{daos_bin} is not built with the Go "valgrind" tag (needs '
                'Go 1.25+ and BUILD_GO_VALGRIND=1), to run under memcheck.')

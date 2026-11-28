"""NLT: build configuration and environment helpers.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import glob
import json
import os
import subprocess  # nosec
import sys
import tempfile
from os.path import join

from .base import CulmTimer, NLTestFail

# Log base for sanitizer reports from processes not run through ValgrindHelper.
SANITIZER_GLOBAL_LOG = 'dnt_asan.global'


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
        self.sanitizers, self.asan_lib = sanitizer_runtime(self._bc['PREFIX'])
        self.memcheck_tool = 'sanitizer' if self.sanitizers else 'valgrind'
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
            try:
                if size.endswith('MiB'):
                    size = int(size[:-3]) * (1024 * 1024)
                elif size.endswith('GiB'):
                    size = int(size[:-3]) * (1024 * 1024 * 1024)
                self.max_log_size = int(size)
            except ValueError as error:
                raise NLTestFail(f'Invalid --max-log-size value {args.max_log_size!r}') from error

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


def find_build_vars():
    """Return the path of the .build_vars.json file"""
    file_self = os.path.dirname(os.path.abspath(__file__))
    while True:
        new_file = join(file_self, '.build_vars.json')
        if os.path.exists(new_file):
            return new_file
        file_self = os.path.dirname(file_self)
        if file_self == '/':
            raise NLTestFail('.build_vars.json file not found')


def load_conf(args):
    """Load the build config file"""
    return NLTConf(find_build_vars(), args)


def sanitizer_runtime(prefix):
    """Return (sanitized, libasan path) for a DAOS install, based on what dfuse links against"""
    # A preloaded libasan is listed without its '=>' path, so check without it.
    env = os.environ.copy()
    env.pop('LD_PRELOAD', None)
    rc = subprocess.run(['ldd', join(prefix, 'bin', 'dfuse')], stdout=subprocess.PIPE,
                        stderr=subprocess.DEVNULL, text=True, env=env, check=False)
    for line in rc.stdout.splitlines():
        if 'libasan.so' in line and '=>' in line:
            return True, line.split('=>')[1].split()[0]
    return False, None


def sanitizer_log_dir(class_name):
    """Return the directory sanitizer reports are written to, matching NLTConf.tmp_dir"""
    if class_name:
        return os.path.abspath(join('nlt_logs', class_name))
    return os.path.abspath('.')


def set_sanitizer_env(env, log_base, leaks=False):
    """Set the ASan runtime options for a process; errors exit with 42 like valgrind is run with"""
    # daos_common and daos_common_pmem are built from the same sources and the engine loads both,
    # so only report ODR violations where the duplicate globals differ in size.
    env['ASAN_OPTIONS'] = (f'exitcode=42:detect_leaks={int(leaks)}:detect_odr_violation=1:'
                           f'log_path={log_base}')


def sanitizer_reports(log_base):
    """Return the sanitizer report files written for log_base"""
    return sorted(glob.glob(f'{log_base}.*'))


def setup_sanitizers(class_name):
    """Prepare this process for a sanitizer build of DAOS.

    pydaos is loaded into this process so the ASan runtime has to be preloaded before Python starts;
    re-exec with it if it is not.  Child processes inherit the preload and the default options.
    """
    with open(find_build_vars(), 'r') as fd:
        prefix = json.load(fd)['PREFIX']
    sanitized, asan_lib = sanitizer_runtime(prefix)
    if not sanitized:
        return
    set_sanitizer_env(os.environ, join(sanitizer_log_dir(class_name), SANITIZER_GLOBAL_LOG))
    preload = os.environ.get('LD_PRELOAD', '')
    if asan_lib and asan_lib not in preload.split(':'):
        os.environ['LD_PRELOAD'] = ':'.join(filter(None, [asan_lib, preload]))
        os.execv(sys.executable, getattr(sys, 'orig_argv', [sys.executable] + sys.argv))


def il_preload(conf, lib):
    """Return LD_PRELOAD for an interception library, keeping the ASan runtime first"""
    path = join(conf['PREFIX'], 'lib64', lib)
    if conf.asan_lib:
        return f'{conf.asan_lib}:{path}'
    return path


def get_base_env(clean=False):
    """Return the base set of env vars needed for DAOS"""
    if clean:
        env = {}
        if 'ASAN_OPTIONS' in os.environ:
            env['ASAN_OPTIONS'] = os.environ['ASAN_OPTIONS']
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
    if conf.sanitizers:
        return
    daos_bin = join(conf['PREFIX'], 'bin', 'daos')
    with open(daos_bin, 'rb') as fd:
        if b'runtime.valgrindRegisterStack' not in fd.read():
            raise NLTestFail(
                f'{daos_bin} is not built with the Go "valgrind" tag (needs '
                'Go 1.25+ and BUILD_GO_VALGRIND=1), to run under memcheck.')

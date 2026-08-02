"""NLT: client fault-injection tests.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

# pylint: disable=too-many-lines

import os
import signal
import subprocess  # nosec
import tempfile
import time
from os.path import join

import yaml

from .base import NLTestFail, NLTestNoFi
from .client import ValgrindHelper, create_cont, run_daos_cmd
from .config import get_base_env, load_conf
from .dfuse import DFuse
from .logging_utils import log_test, setup_log_test
from .reporting import WarningsFactory
from .server import DaosServer
from .watchdog import KILL_GRACE, MEMCHECK_STALL_SECS, STALL_SECS, handle_stalled

# Fault injection testing.
#
# This runs two different commands under fault injection, although it allows
# for more to be added.  The command is defined, then run in a loop with
# different locations enabled, essentially failing each call to
# D_ALLOC() in turn.  This iterates for all memory allocations in the command
# which is around 1300 each command so this takes a while.
#
# In order to improve response times the different locations are run in
# parallel, although the results are processed in order.
#
# Each location is checked for memory leaks according to the log file
# (D_ALLOC/D_FREE not matching), that it didn't crash and some checks are run
# on stdout/stderr as well.
#
# If a particular location caused the command to exit with a signal then that
# location is re-run at the end under valgrind to get better diagnostics.
#


class AllocFailTestRun():
    """Class to run a fault injection command with a single fault"""

    def __init__(self, aft, cmd, env, loc, cwd):

        # The return from subprocess.poll
        self.ret = None
        self.fault_injected = None
        self.loc = loc
        # The valgrind handle
        self.valgrind_hdl = None

        self.dir_handle = None
        self.stdout = None
        self.returncode = None
        self.was_killed = False
        self._start_time = None

        # Set this to disable memory leak checking if the command outputs a DER_BUSY message.  This
        # is to allow tests to leak memory if there are errors during shutdown.
        self.ignore_busy = False

        # The subprocess handle and other private data.
        self._sp = None
        self._cmd = cmd
        self._env = env
        self._aft = aft
        self._fi_file = None
        self._stderr = None
        self._fi_loc = None
        self._cwd = cwd
        self._issues_before = 0

        if loc:
            prefix = f'dnt_{loc:04d}_'
        else:
            prefix = 'dnt_reference_'
        with tempfile.NamedTemporaryFile(prefix=prefix,
                                         suffix='.log',
                                         dir=self._aft.log_dir,
                                         delete=False) as log_file:
            self.log_file = log_file.name
            self._env['D_LOG_FILE'] = self.log_file
            with open(log_file.name, 'w', encoding='utf-8') as lf:
                lf.write(f'cmd: {" ".join(cmd)}\n')

    def __str__(self):
        cmd_text = ' '.join(self._cmd)
        res = f"Fault injection test of '{cmd_text}'\n"
        res += f'Fault injection location {self.loc}\n'
        if self.valgrind_hdl:
            res += 'Valgrind enabled for this test\n'
        if self.returncode is None:
            res += 'Process not completed'
        else:
            res += f'Returncode was {self.returncode}'

        if self.stdout:
            res += f'\nSTDOUT:{self.stdout.decode("utf-8").strip()}'

        if self._stderr:
            res += f'\nSTDERR:{self._stderr.decode("utf-8").strip()}'
        return res

    def start(self):
        """Start the command"""
        faults = {}

        faults['fault_config'] = [{'id': 100,
                                   'probability_x': 1,
                                   'probability_y': 1}]

        if self.loc:
            faults['fault_config'].append({'id': 0,
                                           'probability_x': 1,
                                           'probability_y': 1,
                                           'interval': self.loc,
                                           'max_faults': 1})

            if self._aft.skip_daos_init:
                faults['fault_config'].append({'id': 101, 'probability_x': 1})

        # pylint: disable=consider-using-with
        self._fi_file = tempfile.NamedTemporaryFile(prefix='fi_', suffix='.yaml')

        self._fi_file.write(yaml.dump(faults, encoding='utf=8'))
        self._fi_file.flush()

        self._env['D_FI_CONFIG'] = self._fi_file.name

        if self.valgrind_hdl:
            exec_cmd = self.valgrind_hdl.get_cmd_prefix()
            exec_cmd.extend(self._cmd)
        else:
            exec_cmd = self._cmd

        self._sp = subprocess.Popen(exec_cmd,
                                    env=self._env,
                                    cwd=self._cwd,
                                    stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE)
        self._start_time = time.monotonic()

    def pid(self):
        """Return the pid of the command"""
        return self._sp.pid

    def elapsed(self):
        """Return seconds since the command started"""
        return time.monotonic() - self._start_time

    def hang_kill(self):
        """Kill a wedged command; result checks are skipped on reap"""
        if self._sp.poll() is not None:
            return
        self.was_killed = True
        self._sp.kill()

    def is_dead(self):
        """Return whether the command has exited, without blocking"""
        return self._sp.poll() is not None

    def has_finished(self):
        """Check if the command has completed"""
        if self.returncode is not None:
            return True

        rc = self._sp.poll()
        if rc is None:
            return False
        self._reap(rc)
        return True

    def wait(self, timeout=None):
        """Wait for the command to complete"""
        if self.returncode is not None:
            return

        if timeout is not None:
            try:
                self._reap(self._sp.wait(timeout=timeout))
            except subprocess.TimeoutExpired:
                handle_stalled([self], log_dir=self._aft.log_dir)
                try:
                    self._reap(self._sp.wait(timeout=KILL_GRACE))
                except subprocess.TimeoutExpired:
                    # Blocked in the kernel; record the kill rather than joining the hang.
                    self._post_killed(-signal.SIGKILL)
            return

        self._reap(self._sp.wait())

    def _reap(self, rc):
        """Process a completed command, bypassing result checks for watchdog kills"""
        if self.was_killed:
            self._post_killed(rc)
        else:
            self._post(rc)

    def _post_killed(self, rc):
        """Reap after a watchdog kill; the positive returncode stops a valgrind re-run"""
        print()
        self.returncode = 128 - rc if rc < 0 else rc
        try:
            self.stdout, self._stderr = self._sp.communicate(timeout=KILL_GRACE)
        except subprocess.TimeoutExpired:
            self.stdout = b'<stdout unavailable: pipe held open after kill>'
            self._stderr = b''
        self.fault_injected = True

    def _significant_issues(self, wf):
        """Count findings that warrant keeping this iteration's log.

        Injecting a fault legitimately makes the code log the allocation failure next to the
        fault site, which cart_logtest reports as 'Logging allocation failure' plus a 'Fault
        injection location' summary.  Those are expected consequences, not defects, so they are
        still reported but do not by themselves retain the log.  Every other finding (leaks,
        strict-mode warnings/errors, ...) does.
        """
        total = 0
        for issue in wf.issues:
            if issue.get('description') == 'Logging allocation failure':
                continue
            if issue.get('category') == 'Fault injection location':
                continue
            total += 1
        return total

    def _issue_total(self):
        """Number of significant findings so far, used to tell if this iteration is of interest."""
        total = self._significant_issues(self._aft.conf.wf)
        if self._aft.wf is not self._aft.conf.wf:
            total += self._significant_issues(self._aft.wf)
        return total

    def _prune_log(self, force_keep=False):
        """Keep this iteration's log only if it is interesting; most fault locations are not.

        A run is interesting if it crashed, if a check raised, or if it produced a finding.  A
        clean run - including one where the fault was simply not injected (e.g. the reference run
        and the many boundary iterations past the last allocation) - has no debugging value.
        """
        if self.returncode is None:
            return
        conf = self._aft.conf
        interesting = force_keep or self.returncode < 0 \
            or self._issue_total() > self._issues_before
        if getattr(conf.args, 'keep_logs', False) or interesting:
            conf.compress_file(self.log_file)
        else:
            try:
                os.unlink(self.log_file)
            except FileNotFoundError:
                pass

    def _post(self, rc):
        """Run the completion checks, then keep or drop this iteration's log."""
        self._issues_before = self._issue_total()
        keep = False
        try:
            self._post_checks(rc)
        except Exception:
            # A check raised (unexpected output/return code); keep the log for debugging.
            keep = True
            raise
        finally:
            self._prune_log(force_keep=keep)

    def _post_checks(self, rc):
        """Helper function, called once after command is complete.

        This is where all the checks are performed.
        """
        def _explain():

            if self._aft.conf.tmp_dir:
                log_dir = self._aft.conf.tmp_dir
            else:
                log_dir = '/tmp'

            short_log_file = self.log_file

            if short_log_file.startswith(self.log_file):
                short_log_file = short_log_file[len(log_dir) + 1:]

            self._aft.wf.explain(self._fi_loc, short_log_file, fi_signal)
            self._aft.conf.wf.explain(self._fi_loc, short_log_file, fi_signal)
        # Put in a new-line.
        print()
        self.returncode = rc
        self.stdout = self._sp.stdout.read()
        self._stderr = self._sp.stderr.read()

        show_memleaks = True

        fi_signal = None
        # A negative return code means the process exited with a signal so do
        # not check for memory leaks in this case as it adds noise, right when
        # it's least wanted.
        if rc < 0:
            show_memleaks = False
            fi_signal = -rc

        if self._aft.ignore_busy and self._aft.check_daos_stderr:
            stderr = self._stderr.decode('utf-8').rstrip()
            for line in stderr.splitlines():
                if line.endswith(': Device or resource busy (-1012)'):
                    show_memleaks = False

        try:
            if self.loc:
                wf = self._aft.wf
            else:
                wf = None

            self._fi_loc = log_test(self._aft.conf,
                                    self.log_file,
                                    show_memleaks=show_memleaks,
                                    ignore_busy=self._aft.ignore_busy,
                                    quiet=True,
                                    skip_fi=True,
                                    leak_wf=wf,
                                    defer_prune=True)
            self.fault_injected = True
            assert self._fi_loc
        except NLTestNoFi:
            # If a fault wasn't injected then check output is as expected.
            # It's not possible to log these as warnings, because there is
            # no src line to log them against, so simply assert.
            assert self.returncode == 0, self

            if self._aft.check_post_stdout:
                assert self._stderr == b''
                if self._aft.expected_stdout is not None:
                    assert self.stdout == self._aft.expected_stdout
            self.fault_injected = False
        if self.valgrind_hdl:
            self.valgrind_hdl.convert_xml()
        if not self.fault_injected:
            _explain()
            return

        # Check stderr from a daos command.
        # These should mostly be from the DH_PERROR_SYS or DH_PERROR_DER macros so check for
        # this format.  There may be multiple lines and the two styles may be mixed.
        # These checks will report an error against the line of code that introduced the "leak"
        # which may well only have a loose correlation to where the error was reported.
        if self._aft.check_daos_stderr:

            # The go code will report a stacktrace in some cases on segfault or double-free
            # and these will obviously not be the expected output but are obviously an error,
            # to avoid filling the results with lots of warnings about stderr just include one
            # to say the check is disabled.
            if rc in (-6, -11):
                self._aft.wf.add(self._fi_loc,
                                 'NORMAL',
                                 f"Unable to check stderr because of exit code '{rc}'",
                                 mtype='Crash preventing check')
                _explain()
                return

            stderr = self._stderr.decode('utf-8').rstrip()
            for line in stderr.splitlines():

                # This is what the go code uses.
                if line.endswith(': DER_NOMEM(-1009): Out of memory'):
                    continue

                # This is what the go code uses for system errors.
                if line.endswith(': errno 12 (Cannot allocate memory)'):
                    continue

                # This is what DH_PERROR_DER uses
                if line.endswith(': Out of memory (-1009)'):
                    continue

                # This is what DH_PERROR_SYS uses
                if line.endswith(': Cannot allocate memory (12)'):
                    continue

                if self._aft.ignore_busy and line.endswith(': Device or resource busy (-1012)'):
                    continue

                if 'DER_UNKNOWN' in line:
                    self._aft.wf.add(self._fi_loc,
                                     'HIGH',
                                     f"Incorrect stderr '{line}'",
                                     mtype='Invalid error code used')
                    continue

                self._aft.wf.add(self._fi_loc,
                                 'NORMAL',
                                 f"Malformed stderr '{line}'",
                                 mtype='Malformed stderr')
            _explain()
            return

        if self.returncode == 0 and self._aft.check_post_stdout:
            if self.stdout != self._aft.expected_stdout:
                self._aft.wf.add(self._fi_loc,
                                 'NORMAL',
                                 f"Incorrect stdout '{self.stdout}'",
                                 mtype='Out of memory caused zero exit code with incorrect output')

        if self._aft.check_stderr:
            stderr = self._stderr.decode('utf-8').rstrip()
            if stderr != '' and not stderr.endswith('(-1009): Out of memory') and \
                not stderr.endswith(': errno 12 (Cannot allocate memory)') and \
               'error parsing command line arguments' not in stderr and \
               self.stdout != self._aft.expected_stdout:
                if self.stdout != b'':
                    print(self._aft.expected_stdout)
                    print()
                    print(self.stdout)
                    print()
                self._aft.wf.add(self._fi_loc,
                                 'NORMAL',
                                 f"Incorrect stderr '{stderr}'",
                                 mtype='Out of memory not reported correctly via stderr')
        _explain()


class AllocFailTest():
    # pylint: disable=too-few-public-methods
    """Class to describe fault injection command"""

    def __init__(self, conf, desc, cmd):
        self.conf = conf
        self.cmd = cmd
        self.description = desc
        self.prefix = True
        # Check stdout/error from commands where faults were not injected
        self.check_post_stdout = True
        # Check stderr conforms to daos_hdlr.c style
        self.check_daos_stderr = False
        self.check_stderr = True
        self.expected_stdout = None
        self.ignore_busy = False
        self.single_process = False
        self.use_il = False
        self._use_pil4dfs = None
        self.wf = conf.wf
        # Instruct the fault injection code to skip daos_init().
        self.skip_daos_init = True
        log_dir = f'dnt_fi_{self.description}_logs'
        if conf.tmp_dir:
            self.log_dir = join(conf.tmp_dir, log_dir)
        else:
            self.log_dir = join('/tmp', log_dir)
        try:
            os.mkdir(self.log_dir)
        except FileExistsError:
            pass

    def use_pil4dfs(self, container):
        """Mark test to use pil4dfs and set container"""
        self._use_pil4dfs = container
        self.check_stderr = False

    def launch(self):
        """Run all tests for this command"""

        def _prep(self):
            rc = self._run_cmd(None)
            rc.wait(timeout=STALL_SECS)
            if rc.was_killed:
                raise NLTestFail('prep run (no faults enabled) stalled and was killed')
            self.expected_stdout = rc.stdout
            assert not rc.fault_injected

        # Prep what the expected stdout is by running once without faults
        # enabled.
        _prep(self)

        print('Expected stdout is')
        print(self.expected_stdout)

        # pylint: disable-next=no-member
        num_cores = len(os.sched_getaffinity(0))

        if num_cores < 14:
            max_child = 1
        else:
            max_child = int(num_cores / 4 * 3)

        if self.single_process:
            max_child = 1

        print(f'Maximum number of spawned tests will be {max_child}')

        active = []
        fid = 2
        max_count = 0
        finished = False

        # List of fault identifiers to re-run under valgrind.
        to_rerun = []

        fatal_errors = False

        max_load_avg = 100

        last_progress = time.monotonic()
        stalled_out = False

        # Now run all iterations in parallel up to max_child.  Iterations will be launched
        # in order but may not finish in order, rather they are processed in the order they
        # finish.  After each repetition completes then check for re-launch new processes
        # to keep the pipeline full.
        while not finished or active:

            load_avg, _, _ = os.getloadavg()

            # DAOS-14164 Back off on launching tests if the system is loaded.  If the node is above
            # a certain load average then pause and lower the level of expected parallelism.  If the
            # node is close to the maximum then do not decrease the count but put preference to
            # completing running tests and only launch one test before re-sampling the load average.

            start_this_iteration = 10
            if max_child > 1 and load_avg > 0.8 * max_load_avg:
                start_this_iteration = 1
                if load_avg > max_load_avg:
                    if max_count < max_child:
                        max_child -= 5
                    else:
                        max_child -= 1
                    max_child = max(max_child, 20)
                    print(f"High load average of {load_avg}, "
                          f"pausing and decreasing parallelism to {max_child} {max_count}")
                    if max_child > 20:
                        time.sleep(2)

            if not finished:
                while start_this_iteration > 0 and len(active) < max_child:
                    active.append(self._run_cmd(fid))
                    fid += 1
                    start_this_iteration -= 1

                    max_count = max(max_count, len(active))

            # Now complete as many as have finished.
            for ret in active:
                if not ret.has_finished():
                    continue
                active.remove(ret)
                last_progress = time.monotonic()
                print()
                print(ret)
                if ret.returncode < 0:
                    fatal_errors = True
                    to_rerun.append(ret.loc)

                if not ret.fault_injected:
                    print('Fault injection did not trigger, stopping')
                    finished = True
                break

            if active and time.monotonic() - last_progress > STALL_SECS:
                fatal_errors = True
                stalled_out = True
                finished = True
                handle_stalled(active, log_dir=self.log_dir)
                print('Sweep stalled; abandoning remaining iterations')
                # Reap the children that died; waiting for one that is stuck in
                # the kernel would hang the run.
                for child in active:
                    if not child.has_finished():
                        print(f'Abandoning stuck pid {child.pid()} (loc {child.loc})',
                              flush=True)
                active.clear()

        print(f'Completed, fid {fid}')
        print(f'Max in flight {max_count}/{max_child}')
        if to_rerun:
            print(f'Number of indexes to re-run {len(to_rerun)}')
            if stalled_out:
                print('Skipping valgrind re-runs; the mount did not survive the sweep')
                to_rerun = []

        for fid in to_rerun:
            rerun = self._run_cmd(fid, valgrind=True)
            print(rerun)
            rerun.wait(timeout=MEMCHECK_STALL_SECS)
            if rerun.was_killed and self.conf.args.failfast:
                print(f'--failfast set; skipping remaining re-runs after stall at {fid}')
                break

        return fatal_errors

    def _run_cmd(self, loc, valgrind=False):
        """Run the test with fault injection enabled"""
        cmd_env = get_base_env()

        # Debug flags to enable all memory allocation logging, but as little else as possible.
        # This improves run-time but makes debugging any issues found harder.
        # cmd_env['D_LOG_MASK'] = 'DEBUG'
        # cmd_env['DD_MASK'] = 'mem'
        # del cmd_env['DD_SUBSYS']

        if self.use_il:
            cmd_env['LD_PRELOAD'] = join(self.conf['PREFIX'], 'lib64', 'libioil.so')

        cwd = None
        tmp_dir = None

        if self._use_pil4dfs is not None:
            # pylint: disable-next=consider-using-with
            tmp_dir = tempfile.TemporaryDirectory(prefix='pil4dfs_mount')
            cwd = tmp_dir.name
            cmd_env['DAOS_MOUNT_POINT'] = cwd
            cmd_env['LD_PRELOAD'] = join(self.conf['PREFIX'], 'lib64', 'libpil4dfs.so')
            cmd_env['D_IL_NO_BYPASS'] = '1'
            cmd_env['DAOS_POOL'] = self._use_pil4dfs.pool.id()
            cmd_env['DAOS_CONTAINER'] = self._use_pil4dfs.id()

        cmd_env['DAOS_AGENT_DRPC_DIR'] = self.conf.agent_dir

        if callable(self.cmd):
            cmd = self.cmd(loc)
        else:
            # Take a copy of cmd as it might be modified and we only want that to happen once.
            cmd = list(self.cmd)

        # Disable logging to stderr from the daos tool, the two streams are both checked already
        # but have different formats.
        if cmd[0] == 'daos':
            cmd_env['DD_STDERR'] = 'CRIT'
            cmd[0] = join(self.conf['PREFIX'], 'bin', 'daos')

        aftf = AllocFailTestRun(self, cmd, cmd_env, loc, cwd)
        if valgrind:
            aftf.valgrind_hdl = ValgrindHelper(self.conf, logid=f'fi_{self.description}_{loc}')
            # Turn off leak checking in this case, as we're just interested in why it crashed.
            aftf.valgrind_hdl.full_check = False

        aftf.dir_handle = tmp_dir
        aftf.start()

        return aftf


def test_dfuse_start(server, conf, wf):
    """Start dfuse under fault injection

    This test will check error paths for faults that can occur whilst starting
    dfuse.  To do this it injects a fault into dfuse just before dfuse_session_mount
    so that it always returns immediately rather than registering with the kernel
    and then it runs dfuse up to this point checking the error paths.
    """
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX', label="dfuse_fi_start")

    mount_point = join(conf.dfuse_parent_dir, 'fi-mount')

    os.mkdir(mount_point)

    cmd = [join(conf['PREFIX'], 'bin', 'dfuse'),
           '--mountpoint', mount_point,
           '--pool', pool.id(), '--cont', container.id(), '--foreground', '--thread-count=2']

    test_cmd = AllocFailTest(conf, 'dfuse', cmd)
    test_cmd.wf = wf
    test_cmd.skip_daos_init = False
    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False  # Checked.

    rc = test_cmd.launch()
    os.rmdir(mount_point)
    return rc


def test_alloc_fail_copy(server, conf, wf):
    """Run container (filesystem) copy under fault injection.

    This test will create a new uuid per iteration, and the test will then try to create a matching
    container so this is potentially resource intensive.

    Create an initial container to copy from so this is testing reading as well as writing

    see also test_alloc_fail_copy_trunc() which is similar but truncates existing files.
    """

    def get_cmd(cont_id):
        return ['daos',
                'filesystem',
                'copy',
                '--src',
                f'daos://{pool.id()}/aft_base',
                '--dst',
                f'daos://{pool.id()}/container_{cont_id}']

    pool = server.get_test_pool_obj()
    with tempfile.TemporaryDirectory(prefix='copy_src_',) as src_dir:
        sub_dir = join(src_dir, 'new_dir')
        os.mkdir(sub_dir)

        for idx in range(5):
            with open(join(sub_dir, f'file.{idx}'), 'w') as ofd:
                ofd.write('hello')

        os.symlink('broken', join(sub_dir, 'broken_s'))
        os.symlink('file.0', join(sub_dir, 'link'))

        rc = run_daos_cmd(conf, ['filesystem', 'copy', '--src', sub_dir,
                                 '--dst', f'daos://{pool.id()}/aft_base'])
        assert rc.returncode == 0, rc

    test_cmd = AllocFailTest(conf, 'filesystem-copy', get_cmd)
    test_cmd.wf = wf
    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False
    # Set the ignore_busy flag so that memory leaks on shutdown are ignored in some cases.
    test_cmd.ignore_busy = True

    return test_cmd.launch()


def test_alloc_fail_copy_trunc(server, conf, wf):
    """Run container (filesystem) copy under fault injection.

    Use filesystem copy to truncate a file.

    Create an initial container to modify, pre-populate it with a number of files of known length
    then have each iteration of the test truncate one file.
    """
    # The number of files to pre-create.  This just needs to be bigger than the iteration count
    # however too many will consume extra resources.
    files_needed = 4000

    def get_cmd(_):
        cmd = ['daos', 'filesystem', 'copy', '--src', src_file.name,
               '--dst', f'daos://{pool.id()}/aftc/new_dir/file.{get_cmd.idx}']
        get_cmd.idx += 1
        assert get_cmd.idx <= files_needed
        return cmd

    get_cmd.idx = 0  # pylint: disable=invalid-name

    pool = server.get_test_pool_obj()
    with tempfile.TemporaryDirectory(prefix='copy_src_',) as src_dir:
        sub_dir = join(src_dir, 'new_dir')
        os.mkdir(sub_dir)

        for idx in range(files_needed):
            with open(join(sub_dir, f'file.{idx}'), 'w') as ofd:
                ofd.write('hello')

        rc = run_daos_cmd(conf, ['filesystem', 'copy', '--src', sub_dir,
                                 '--dst', f'daos://{pool.id()}/aftc'])
        assert rc.returncode == 0, rc

    with tempfile.NamedTemporaryFile() as src_file:

        test_cmd = AllocFailTest(conf, 'filesystem-copy-trunc', get_cmd)
        test_cmd.wf = wf
        test_cmd.check_daos_stderr = True
        test_cmd.check_post_stdout = False
        # Set the ignore_busy flag so that memory leaks on shutdown are ignored in some cases.
        test_cmd.ignore_busy = True

        return test_cmd.launch()


def test_alloc_pil4dfs_ls(server, conf, wf):
    """Run pil4dfs under fault injection

    Create a pool and populate a subdir with a number of entries, files, symlink (broken and not)
    and another subdir.  Run 'ls' on this to see the output.
    """
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX', label='pil4dfs_fi')

    with tempfile.TemporaryDirectory(prefix='pil4_src_',) as src_dir:
        sub_dir = join(src_dir, 'new_dir')
        os.mkdir(sub_dir)

        for idx in range(5):
            with open(join(sub_dir, f'file.{idx}'), 'w') as ofd:
                ofd.write('hello')

        os.mkdir(join(sub_dir, 'new_dir'))
        os.symlink('broken', join(sub_dir, 'broken_s'))
        os.symlink('file.0', join(sub_dir, 'link'))

        rc = run_daos_cmd(conf, ['filesystem', 'copy', '--src', f'{src_dir}/new_dir',
                                 '--dst', f'daos://{pool.id()}/{container.id()}'])
        print(rc)
        assert rc.returncode == 0, rc

    test_cmd = AllocFailTest(conf, 'pil4dfs-ls', ['ls', '-l', 'new_dir/'])
    test_cmd.wf = wf
    test_cmd.use_pil4dfs(container)
    test_cmd.check_daos_stderr = False
    test_cmd.check_post_stdout = False

    return test_cmd.launch()


def test_alloc_cont_create(server, conf, wf):
    """Run container creation under fault injection.

    This test will create a new uuid per iteration, and the test will then try to create a matching
    container so this is potentially resource intensive.
    """
    pool = server.get_test_pool_obj()

    def get_cmd(cont_id):
        return ['daos',
                'container',
                'create',
                pool.id(),
                '--properties',
                f'srv_cksum:on,label:{cont_id},rd_fac:0']

    test_cmd = AllocFailTest(conf, 'cont-create', get_cmd)
    test_cmd.wf = wf
    test_cmd.check_post_stdout = False

    return test_cmd.launch()


def test_alloc_fail_cont_create(server, conf):
    """Run container create --path under fault injection."""
    pool = server.get_test_pool_obj()
    container = create_cont(conf, pool, ctype='POSIX', label='parent_cont')

    dfuse = DFuse(server, conf, container=container)
    dfuse.use_valgrind = False
    dfuse.start()

    def get_cmd(cont_id):
        return ['daos',
                'container',
                'create',
                '--type',
                'POSIX',
                '--path',
                join(dfuse.dir, f'container_{cont_id}'),
                '--attrs',
                ','.join([
                    'dfuse-attr-time:5m',
                    'dfuse-dentry-time:4m',
                    'dfuse-dentry-dir-time:3m',
                    'dfuse-ndentry-time:2m',
                    'dfuse-data-cache:off',
                    'dfuse-direct-io-disable:off'])]

    test_cmd = AllocFailTest(conf, 'cont-create', get_cmd)
    test_cmd.check_post_stdout = False

    rc = test_cmd.launch()
    dfuse.stop()
    return rc


def test_alloc_fail_cat(server, conf):
    """Run the Interception library with fault injection

    Start dfuse for this test, and do not do output checking on the command
    itself yet.
    """
    pool = server.get_test_pool_obj()
    container = create_cont(conf, pool, ctype='POSIX', label='fault_inject')

    dfuse = DFuse(server, conf, container=container)
    dfuse.use_valgrind = False
    dfuse.start()

    target_file = join(dfuse.dir, 'test_file')

    with open(target_file, 'w') as fd:
        fd.write('Hello there')

    test_cmd = AllocFailTest(conf, 'il-cat', ['cat', target_file])
    test_cmd.use_il = True
    test_cmd.wf = conf.wf

    rc = test_cmd.launch()
    dfuse.stop()
    return rc


def test_alloc_fail_il_cp(server, conf):
    """Run the Interception library with fault injection

    Start dfuse for this test, and do not do output checking on the command itself yet.
    """
    pool = server.get_test_pool_obj()
    container = create_cont(conf, pool, ctype='POSIX', label='il_cp')

    dfuse = DFuse(server, conf, container=container)
    dfuse.use_valgrind = False
    dfuse.start()

    test_dir = join(dfuse.dir, 'test_dir')

    os.mkdir(test_dir)

    cmd = ['fs', 'set-attr', '--path', test_dir, '--oclass', 'S4', '--chunk-size', '8']

    rc = run_daos_cmd(conf, cmd)
    print(rc)

    src_file = join(test_dir, 'src_file')

    with open(src_file, 'w') as fd:
        fd.write('Some raw test data that spans over at least two targets and possibly more.')

    def get_cmd(loc):
        return ['cp', src_file, join(test_dir, f'test_{loc}')]

    test_cmd = AllocFailTest(conf, 'il-cp', get_cmd)
    test_cmd.use_il = True
    test_cmd.wf = conf.wf

    rc = test_cmd.launch()
    dfuse.stop()
    container.destroy()
    return rc


def test_fi_list_attr(server, conf, wf):
    """Run daos cont list-attr with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, label="attr_cont")

    container.set_attrs({'my-test-attr-1': 'some-value',
                        'my-test-attr-2': 'some-other-value'})

    cmd = ['daos',
           'container',
           'list-attrs',
           pool.id(),
           container.id()]

    test_cmd = AllocFailTest(conf, 'cont-list-attr', cmd)
    test_cmd.wf = wf

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_fi_get_prop(server, conf, wf):
    """Run daos cont get-prop with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX', label="prop_cont")

    cmd = ['daos',
           'container',
           'get-prop',
           pool.id(),
           container.id()]

    test_cmd = AllocFailTest(conf, 'cont-get-prop', cmd)
    test_cmd.wf = wf
    test_cmd.check_post_stdout = False  # Checked.

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_fi_get_attr(server, conf, wf):
    """Run daos cont get-attr with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, label="getattr-cont")

    attr_name = 'my-test-attr'

    container.set_attrs({attr_name: 'value'})

    cmd = ['daos',
           'container',
           'get-attr',
           pool.id(),
           container.id(),
           attr_name]

    test_cmd = AllocFailTest(conf, 'cont-get-attr', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_fi_cont_query(server, conf, wf):
    """Run daos cont query with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX', label="cont_query")

    cmd = ['daos',
           'container',
           'query',
           pool.id(),
           container.id()]

    test_cmd = AllocFailTest(conf, 'cont-query', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_fi_cont_check(server, conf, wf):
    """Run daos cont check with fault injection"""
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, label="cont_check")

    cmd = ['daos',
           'container',
           'check',
           pool.id(),
           container.id()]

    test_cmd = AllocFailTest(conf, 'cont-check', cmd)
    test_cmd.wf = wf

    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_alloc_fail(server, conf):
    """Run 'daos' client binary with fault injection"""
    pool = server.get_test_pool_obj()

    cmd = ['daos',
           'cont',
           'list',
           pool.id()]
    test_cmd = AllocFailTest(conf, 'pool-list-containers', cmd)

    # Create at least one container, and record what the output should be when
    # the command works.
    container = create_cont(conf, pool, label="listing_container")

    rc = test_cmd.launch()
    container.destroy()
    return rc


def test_dfs_check(server, conf, wf):
    """Run filesystem check.

    Create a pool and populate a subdir with a number of entries, files, symlink (broken and not)
    and another subdir.  Run 'daos filesystem check' on this to see the output.
    """
    pool = server.get_test_pool_obj()

    container = create_cont(conf, pool, ctype='POSIX', label='fsck')

    with tempfile.TemporaryDirectory(prefix='fsck_src_',) as src_dir:
        sub_dir = join(src_dir, 'new_dir')
        os.mkdir(sub_dir)

        for idx in range(5):
            with open(join(sub_dir, f'file.{idx}'), 'w') as ofd:
                ofd.write('hello')

        os.mkdir(join(sub_dir, 'new_dir'))
        # os.symlink('broken', join(sub_dir, 'broken_s'))
        os.symlink('file.0', join(sub_dir, 'link'))

        rc = run_daos_cmd(conf, ['filesystem', 'copy', '--src', f'{src_dir}/new_dir',
                                 '--dst', f'daos://{pool.id()}/{container.id()}'])
        print(rc)
        assert rc.returncode == 0, rc

    test_cmd = AllocFailTest(
        conf, 'fs-check', ['daos', 'filesystem', 'check', pool.id(), container.id()])
    test_cmd.wf = wf
    test_cmd.single_process = True
    test_cmd.check_daos_stderr = True
    test_cmd.check_post_stdout = False

    return test_cmd.launch()


def server_fi(args):
    """Run the server under fault injection.

    Start the server, create a container, enable periodic failing of D_ALLOC() and then perform
    I/O.  At some point this could be extended to checking the client also behaves properly but
    for now just check the server logs.

    This is not run in CI yet so needs to run manually.  As it's probabilistic then it can be
    expected to find more issues based on how often it's run so it is not suitable for PRs
    but should be run for long periods of time.
    """
    conf = load_conf(args)

    wf = WarningsFactory('nlt-errors.json', post_error=True, check='Server FI testing')

    args.dfuse_debug = 'INFO'
    args.client_debug = 'INFO'
    args.memcheck = 'no'

    conf.set_wf(wf)
    conf.set_args(args)
    setup_log_test(conf)

    with DaosServer(conf, wf=wf, test_class='server-fi', enable_fi=True) as server:

        pool = server.get_test_pool_obj()
        cont = create_cont(conf, pool=pool, ctype='POSIX', label='server_test')

        # Instruct the server to fail a % of allocations.
        server.set_fi(probability=1)

        for idx in range(100):
            server.run_daos_client_cmd_pil4dfs(
                ['touch', f'file.{idx}'], container=cont, check=False, report=False)
            server.run_daos_client_cmd_pil4dfs(
                ['dd', 'if=/dev/zero', f'of=file.{idx}', 'bs=1', 'count=1024'],
                container=cont, check=False, report=False)
            server.run_daos_client_cmd_pil4dfs(
                ['rm', '-f', f'file.{idx}'], container=cont, check=False, report=False)

        # Turn off fault injection again to assist in server shutdown.
        server.set_fi(probability=0)
        server.set_fi(probability=0)

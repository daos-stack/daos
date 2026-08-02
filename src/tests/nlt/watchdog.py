"""NLT: stall detection and diagnostics for wedged child processes.

(C) Copyright 2026 Hewlett Packard Enterprise Development LP

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import signal
import subprocess  # nosec
import sys
import threading
import time
import traceback
from os.path import join

# How long a process may show no sign of progress before it is declared stalled.
STALL_SECS = int(os.environ.get('NLT_STALL_SECS', '300'))
# Whole-command bound for a complete run under memcheck; unlike the per-request
# wedge checks this covers an entire (heavily slowed) process lifetime.
MEMCHECK_STALL_SECS = STALL_SECS * 6

# Bounds on the stall diagnostics themselves.
DIAG_TIMEOUT = 60
STACK_READ_TIMEOUT = 15
DUMP_DEADLINE_SECS = 120

# How long a killed process gets to shut down; longer than this is assumed to be hung.
KILL_GRACE = 30

# How often the wedge watchdog samples kernel state.
WATCH_INTERVAL = 20


def _run_diag(cmd, timeout=DIAG_TIMEOUT):
    """Run a diagnostic command returning (exit code, output), never raising or blocking

    A command that outlives timeout is killed and reports a nonzero code.
    """
    try:
        # pylint: disable-next=consider-using-with
        proc = subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, start_new_session=True)
    except OSError as err:
        return 127, f'<{cmd[0]} failed: {err}>'

    def output(wait_secs):
        return proc.communicate(timeout=wait_secs)[0].decode('utf-8', errors='replace').rstrip()

    try:
        text = output(timeout)
    except subprocess.TimeoutExpired:
        # Overdue: kill the process group and salvage whatever it wrote.
        try:
            os.killpg(proc.pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            return -1, f'{output(KILL_GRACE)}\n<{cmd[0]} exceeded {timeout}s>'
        except subprocess.TimeoutExpired:
            proc.stdout.close()
            return -1, f'<{cmd[0]} exceeded {timeout}s and could not be reaped>'
    return proc.returncode, text


def _read_file(path):
    """Return the stripped content of a small /proc or /sys file, or None"""
    try:
        with open(path, encoding='utf-8', errors='replace') as pfile:
            return pfile.read().strip()
    except OSError:
        return None


def _proc_state(pid):
    """Return blocked system-call state for a pid from /proc"""
    out = []
    for name in ('wchan', 'syscall'):
        out.append(f'{name}={_read_file(f"/proc/{pid}/{name}") or "<unreadable>"}')
    status = _read_file(f'/proc/{pid}/status') or ''
    for line in status.splitlines():
        if line.startswith(('State:', 'Threads:')):
            out.append(line.strip())
    return ' '.join(out)


def _proc_threads(pid, deadline=None):
    """Report every thread of a process from /proc; debuggers cannot attach in D state"""
    lines = []
    stack_err = None
    try:
        tids = sorted(os.listdir(f'/proc/{pid}/task'), key=int)
    except OSError as err:
        return f'<cannot list threads of {pid}: {err}>'
    for tid in tids:
        if deadline is not None and time.monotonic() > deadline:
            lines.append(f'  <dump deadline reached; skipping remaining of {len(tids)} threads>')
            break
        base = f'/proc/{pid}/task/{tid}'
        fields = []
        for name in ('comm', 'wchan'):
            fields.append(f'{name}={_read_file(f"{base}/{name}") or "?"}')
        # The state field follows the parenthesized command name, which can itself
        # contain ') ', so anchor on the last occurrence per proc(5).
        state = (_read_file(f'{base}/stat') or '').rpartition(') ')[2].split()
        if state:
            fields.append(f'state={state[0]}')
        lines.append(f'  TID {tid}: ' + ' '.join(fields))
        rc, kstack = _run_diag(['sudo', 'cat', f'{base}/stack'], timeout=STACK_READ_TIMEOUT)
        if rc == 0 and kstack:
            for kline in kstack.splitlines()[:12]:
                lines.append(f'      {kline.strip()}')
        elif rc != 0 and stack_err is None:
            stack_err = (kstack or '<no output>').splitlines()[0].strip()
            lines.append(f'  <kernel stack reads failing: rc={rc} {stack_err}>')
    return '\n'.join(lines)


def exit_now(code):
    """Terminate without interpreter shutdown, whose cleanup can block on a wedged mount"""
    sys.stdout.flush()
    sys.stderr.flush()
    os._exit(code)  # pylint: disable=protected-access


def dump_stalled(tasks, log_dir=None):
    """Dump diagnostics for wedged processes given as (pid, description) pairs"""
    dump_file = None
    if log_dir:
        try:
            # The dnt*.log name is what CI's log collection keeps; append across stalls.
            # pylint: disable-next=consider-using-with
            dump_file = open(join(log_dir, 'dnt_stall_dump.log'), 'a', encoding='utf-8')
        except OSError:
            dump_file = None

    def emit(text):
        # File first; a stalled stdout consumer must not cost us the dump.
        if dump_file:
            try:
                dump_file.write(f'{text}\n')
                dump_file.flush()
            except OSError:
                pass
        print(text, flush=True)

    try:
        deadline = time.monotonic() + DUMP_DEADLINE_SECS
        emit(f'\n===== NLT STALL DETECTED {time.strftime("%Y-%m-%d %H:%M:%S")} =====')
        # /proc cannot block, so gather it before anything that can.
        for pid, desc in tasks:
            emit(f'--- stalled: pid={pid} {desc}')
            emit(_proc_threads(pid, deadline=deadline))
        daemons = _run_diag(['pgrep', '-a', 'daos_engine|daos_agent|dfuse'])[1]
        emit(f'--- daemons:\n{daemons}')
        daemon_pids = [int(line.split()[0]) for line in daemons.splitlines()
                       if line and line.split()[0].isdigit()]
        for pid in daemon_pids:
            if time.monotonic() > deadline:
                emit('<dump deadline reached; skipping remaining daemons>')
                break
            emit(f'--- daemon pid={pid}:')
            emit(_proc_threads(pid, deadline=deadline))
        emit('--- process tree:')
        emit(_run_diag(['ps', 'auxwwf'])[1])
        emit('===== NLT STALL DUMP COMPLETE =====')
    except Exception as err:  # pylint: disable=broad-except
        emit(f'===== NLT STALL DUMP ABORTED: {err!r} =====')
        traceback.print_exc(file=sys.stdout)
        sys.stdout.flush()
    finally:
        if dump_file:
            dump_file.close()


def dfuse_connection_ids():
    """Return the FUSE connection ids of dfuse mounts, mapped to their mount points"""
    ids = {}
    # Safer than stat, which could block on a wedged mount
    for line in (_read_file('/proc/self/mountinfo') or '').splitlines():
        fields = line.split()
        if '-' in fields and 'fuse.daos' in fields[fields.index('-'):]:
            ids[fields[2].split(':')[1]] = fields[4]
    return ids


def abort_fuse_connections(only=None):
    """Abort every backed-up dfuse connection, failing its requests so blocked processes can die

    only: if given, restrict aborts to these connection ids.
    """
    done = []
    aborted = 0
    dfuse_conns = dfuse_connection_ids()
    try:
        conns = sorted(os.listdir('/sys/fs/fuse/connections'))
    except OSError as err:
        return 0, [f'<cannot list fuse connections: {err}>']
    for conn in conns:
        if conn not in dfuse_conns:
            continue
        if only is not None and conn not in only:
            continue
        waiting = _read_file(f'/sys/fs/fuse/connections/{conn}/waiting')
        if waiting is None:
            done.append(f'<cannot read waiting count of dfuse connection {conn}>')
            continue
        if waiting in ('', '0'):
            continue
        path = f'/sys/fs/fuse/connections/{conn}/abort'
        rc, res = _run_diag(['sudo', 'sh', '-c', f'echo 1 > {path}'])
        if rc == 0:
            aborted += 1
            done.append(f'aborted dfuse connection {conn} (waiting={waiting}): {res or "ok"}')
        else:
            done.append(f'<abort of dfuse connection {conn} failed rc={rc}: {res}>')
    return aborted, (done or ['<no backed-up dfuse connections found>'])


def handle_stalled(active, log_dir=None):
    """Dump diagnostics then clear the wedged children"""
    dump_stalled([(child.pid(), f'loc={child.loc} elapsed={child.elapsed():.0f}s')
                  for child in active], log_dir=log_dir)
    for child in active:
        child.hang_kill()

    def _find_zombies(wait_sec):
        deadline = time.monotonic() + wait_sec
        alive = list(active)
        while alive and time.monotonic() < deadline:
            alive = [c for c in alive if not c.is_dead()]
            if alive:
                time.sleep(1)
        return alive

    zombies = _find_zombies(KILL_GRACE)
    if zombies:
        # A child that outlives the kill grace is likely blocked in the kernel.
        # We can try to free it up by failing its FUSE requests so that it
        # can exit.
        print(f'{len(zombies)} child(ren) survived the kill; '
              f'aborting backed-up FUSE connections to release them', flush=True)
        aborted, lines = abort_fuse_connections()
        for line in lines:
            print(line, flush=True)
        if aborted:
            zombies = _find_zombies(KILL_GRACE)

    for child in zombies:
        print(f'WARNING: pid {child.pid()} (loc {child.loc}) survived SIGKILL: '
              f'{_proc_state(child.pid())}', flush=True)


def _fuse_blocked(wchan):
    """Return whether a wait channel is a FUSE request wait"""
    return wchan is not None and (wchan == 'request_wait_answer' or wchan.startswith('fuse_'))


def _descendant_pids(root):
    """Return root and every live descendant process id"""
    children = {}
    for entry in os.listdir('/proc'):
        if not entry.isdigit():
            continue
        stat = _read_file(f'/proc/{entry}/stat')
        if not stat:
            continue
        fields = stat.rpartition(') ')[2].split()
        if len(fields) < 2:
            continue
        children.setdefault(int(fields[1]), []).append(int(entry))
    pids = []
    queue = [root]
    while queue:
        pid = queue.pop()
        pids.append(pid)
        queue.extend(children.get(pid, []))
    return pids


def _voluntary_switches(pid, tid):
    """Return a task's voluntary context-switch count, or None"""
    status = _read_file(f'/proc/{pid}/task/{tid}/status') or ''
    for line in status.splitlines():
        if line.startswith('voluntary_ctxt_switches:'):
            return line.split()[-1]
    return None


class WedgeWatch(threading.Thread):
    """Detect tasks wedged on FUSE requests anywhere in the NLT process tree.

    A task is wedged when it sits in the same FUSE wait channel with an unchanged
    voluntary context-switch count for the whole threshold window; a busy-but-healthy
    task advances its counters with every request.  Detection reads kernel state, so
    tests need no timeout or heartbeat plumbing.  On a wedge: dump diagnostics, then
    either end the run (--failfast, nothing to abort, or a repeat wedge) or abort just
    the backed-up mounts so their blocked calls fail and surviving tests still report.
    """

    def __init__(self, failfast, log_dir=None, report=None, finalize=None,
                 threshold=STALL_SECS):
        super().__init__(name='nlt-wedge-watch', daemon=True)
        self._failfast = failfast
        self._log_dir = log_dir
        self._report = report
        self._finalize = finalize
        self._threshold = threshold
        self._tasks = {}
        self._conns = {}
        self._fired = False

    def run(self):
        while True:
            time.sleep(WATCH_INTERVAL)
            try:
                self._check()
            except Exception as err:  # pylint: disable=broad-except
                # The watchdog must never take down a healthy run.
                print(f'wedge watchdog error (ignored): {err!r}', flush=True)

    def _sample(self):
        """Update task and connection tracking; return what has been parked too long"""
        now = time.monotonic()
        seen = set()
        wedged = []
        for pid in _descendant_pids(os.getpid()):
            try:
                tids = os.listdir(f'/proc/{pid}/task')
            except OSError:
                continue
            for tid in tids:
                wchan = _read_file(f'/proc/{pid}/task/{tid}/wchan')
                if not _fuse_blocked(wchan):
                    self._tasks.pop((pid, tid), None)
                    continue
                seen.add((pid, tid))
                switches = _voluntary_switches(pid, tid)
                prev = self._tasks.get((pid, tid))
                if prev and prev[0] == wchan and prev[1] == switches:
                    if now - prev[2] > self._threshold:
                        wedged.append((pid, tid, wchan, now - prev[2]))
                else:
                    self._tasks[(pid, tid)] = (wchan, switches, now)
        for key in [key for key in self._tasks if key not in seen]:
            del self._tasks[key]

        current = {}
        for conn in dfuse_connection_ids():
            waiting = _read_file(f'/sys/fs/fuse/connections/{conn}/waiting')
            if waiting not in (None, '', '0'):
                current[conn] = self._conns.get(conn, now)
        self._conns = current
        pinned = [conn for conn, since in current.items() if now - since > self._threshold]
        return wedged, pinned

    def _check(self):
        wedged, pinned = self._sample()
        if not wedged:
            return
        notes = {}
        for pid, tid, wchan, age in wedged:
            notes.setdefault(pid, []).append(f'TID {tid} parked {age:.0f}s in {wchan}')
        dump_stalled([(pid, '; '.join(lines)) for pid, lines in notes.items()],
                     log_dir=self._log_dir)
        detail = '\n'.join(f'pid {pid}: {"; ".join(lines)}' for pid, lines in notes.items())
        if self._report:
            self._report('FUSE wedge detected', detail)
        if self._failfast or self._fired or not pinned:
            print('Wedge watchdog: ending the run', flush=True)
            if self._finalize:
                self._finalize()
            exit_now(1)
        self._fired = True
        _, lines = abort_fuse_connections(only=pinned)
        for line in lines:
            print(line, flush=True)
        # Re-measure from scratch; a cleared wedge must not re-fire on stale state.
        self._tasks.clear()
        self._conns.clear()

"""NLT: shared exceptions, ids, per-thread test context and small helpers.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import subprocess  # nosec
import threading
import time


class NLTestFail(Exception):
    """Used to indicate test failure"""

    def __repr__(self):
        return "Test failure object for NLT"


class NLTestNoFi(NLTestFail):
    """Used to indicate Fault injection didn't work"""


class NLTestIlZeroCall(NLTestFail):
    """Used to indicate a function did not log anything"""

    def __init__(self, function):
        super().__init__(self)
        self.function = function
        self.command = []

    def __str__(self):
        if self.command:
            cmd = ' '.join(self.command)
            return f'Command "{cmd}" has zero stat count for {self.function}'
        return f"Called program did not call {self.function}"


class NLTestTimeout(NLTestFail):
    """Used to indicate that an operation timed out"""


instance_num = 0  # pylint: disable=invalid-name
_instance_lock = threading.Lock()


def get_inc_id():
    """Return a unique character"""
    global instance_num  # pylint: disable=invalid-name
    # Called from the parallel POSIX test threads, so guard the counter.
    with _instance_lock:
        instance_num += 1
        return f'{instance_num:04d}'


# Name of the test currently executing on this thread, used to correlate log-analysis findings
# back to the test that produced them.  POSIX tests run in parallel threads, so this is per-thread.
_active_test = threading.local()


def set_active_test(name):
    """Record the test owning any log-analysis findings raised on this thread"""
    _active_test.name = name


def get_active_test():
    """Return the test owning findings on this thread, or None for shared/server-wide logs"""
    return getattr(_active_test, 'name', None)


def umount(path, background=False):
    """Umount dfuse from a given path"""
    if background:
        cmd = ['fusermount3', '-uz', path]
    else:
        cmd = ['fusermount3', '-u', path]
    ret = subprocess.run(cmd, check=False)
    print(f'rc from {" ".join(cmd[:2])} {ret.returncode}')
    return ret.returncode


class CulmTimer():
    """Class to keep track of elapsed time so we know where to focus performance tuning"""

    def __init__(self):
        self.total = 0
        self._start = None

    def start(self):
        """Start the timer"""
        self._start = time.perf_counter()

    def stop(self):
        """Stop the timer, and add elapsed to total"""
        self.total += time.perf_counter() - self._start


class BoolRatchet():
    """Used for saving test results"""

    # Any call to fail() of add_result with a True value will result
    # in errors being True.

    def __init__(self):
        self.errors = False

    def fail(self):
        """Mark as failure"""
        self.errors = True

    def add_result(self, result):
        """Save result, keep record of failure"""
        if result:
            self.fail()

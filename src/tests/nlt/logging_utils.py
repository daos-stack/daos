"""NLT: log capture and DAOS log-file analysis.

(C) Copyright 2020-2024 Intel Corporation.
(C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
(C) Copyright 2025 Google LLC
(C) Copyright 2025 Enakta Labs Ltd

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os
import sys
import threading
from os.path import join

from .base import NLTestFail, NLTestIlZeroCall, NLTestNoFi


class NltStdoutWrapper():
    """Class for capturing stdout from threads"""

    def __init__(self):
        self._stdout = sys.stdout
        self._outputs = {}
        sys.stdout = self

    def write(self, value):
        """Print to stdout.  If this is the main thread then print it, always save it"""
        value = str(value)
        thread = threading.current_thread()
        if not thread.daemon:
            self._stdout.write(value)
        thread_id = thread.ident
        try:
            self._outputs[thread_id] += value
        except KeyError:
            self._outputs[thread_id] = value

    def sprint(self, value):
        """Really print something to stdout"""
        self._stdout.write(str(value) + '\n')

    def get_thread_output(self):
        """Return the stdout by the calling thread, and reset for next time"""
        return self._outputs.pop(threading.get_ident(), None)

    def flush(self):
        """Flush"""
        self._stdout.flush()

    def __del__(self):
        sys.stdout = self._stdout


class NltStderrWrapper():
    """Class for capturing stderr from threads"""

    def __init__(self):
        self._stderr = sys.stderr
        self._outputs = {}
        sys.stderr = self

    def write(self, value):
        """Print to stderr.  Always print it, always save it"""
        value = str(value)
        thread = threading.current_thread()
        self._stderr.write(value)
        thread_id = thread.ident
        try:
            self._outputs[thread_id] += value
        except KeyError:
            self._outputs[thread_id] = value

    def get_thread_err(self):
        """Return the stderr by the calling thread, and reset for next time"""
        return self._outputs.pop(threading.get_ident(), None)

    def flush(self):
        """Flush"""
        self._stderr.flush()

    def __del__(self):
        sys.stderr = self._stderr


nlt_lp = None  # pylint: disable=invalid-name
nlt_lt = None  # pylint: disable=invalid-name
nlt_ct = None  # pylint: disable=invalid-name


def setup_log_test(conf):
    """Setup and import the log tracing code"""
    # Try and pick this up from the src tree if possible; src/tests/ is the parent of this package.
    file_self = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    logparse_dir = join(file_self, 'ftest/cart/util')
    crt_mod_dir = os.path.realpath(logparse_dir)
    if crt_mod_dir not in sys.path:
        sys.path.append(crt_mod_dir)

    # Or back off to the install dir if not.
    logparse_dir = join(conf['PREFIX'], 'lib/daos/TESTING/ftest/cart')
    crt_mod_dir = os.path.realpath(logparse_dir)
    if crt_mod_dir not in sys.path:
        sys.path.append(crt_mod_dir)

    global nlt_lp  # pylint: disable=invalid-name
    global nlt_lt  # pylint: disable=invalid-name
    global nlt_ct  # pylint: disable=invalid-name

    nlt_lp = __import__('cart_logparse')
    nlt_lt = __import__('cart_logtest')
    ct_mod = __import__('cart_logusage')

    nlt_ct = ct_mod.UsageTracer()

    if conf.args.log_usage_import:
        if os.path.exists(conf.args.log_usage_import):
            nlt_ct.load(conf.args.log_usage_import)
        else:
            print(f'Unable to load log-usage input file {conf.args.log_usage_import}')

    nlt_lt.wf = conf.wf


def close_log_test(conf):
    """Close down the log tracing"""
    conf.flush_bz2()

    if conf.args.log_usage_save:
        nlt_ct.report_all(conf.args.log_usage_save)

    if conf.args.log_usage_export:
        nlt_ct.save(conf.args.log_usage_export)


def log_timer(func):
    """Wrapper around the log_test function to measure how long it takes"""

    def log_timer_wrapper(*args, **kwargs):
        """Do the actual wrapping"""
        conf = args[0]
        conf.log_timer.start()
        rc = None
        try:
            rc = func(*args, **kwargs)
        finally:
            conf.log_timer.stop()
        return rc

    return log_timer_wrapper


@log_timer
def log_test(conf,
             filename,
             show_memleaks=True,
             quiet=False,
             skip_fi=False,
             leak_wf=None,
             ignore_einval=False,
             ignore_busy=False,
             check_read=False,
             check_write=False,
             check_fstat=False,
             defer_prune=False):
    """Run the log checker on filename, logging to stdout"""
    # pylint: disable=too-many-arguments

    # Check if the log file has wrapped, if it has then log parsing checks do
    # not work correctly.

    # https://stackoverflow.com/questions/1094841/get-human-readable-version-of-file-size
    def sizeof_fmt(num, suffix='B'):
        """Return size as a human readable string"""
        # pylint: disable=consider-using-f-string
        for unit in ['', 'Ki', 'Mi', 'Gi', 'Ti', 'Pi', 'Ei', 'Zi']:
            if abs(num) < 1024.0:
                return "%3.1f%s%s" % (num, unit, suffix)
            num /= 1024.0
        return "%.1f%s%s" % (num, 'Yi', suffix)

    if os.path.exists(f'{filename}.old'):
        raise NLTestFail(f'Log file exceeded max size: {filename}')
    fstat = os.stat(filename)
    if fstat.st_size == 0:
        os.unlink(filename)
        return None
    if not quiet:
        print(f'Running log_test on {filename} {sizeof_fmt(fstat.st_size)}')

    log_iter = nlt_lp.LogIter(filename)

    lto = nlt_lt.LogTest(log_iter, quiet=quiet)

    # Add the code coverage tracer.
    lto.add_tracer(nlt_ct, None)

    lto.hide_fi_calls = skip_fi

    if ignore_einval:
        lto.skip_suffixes.append(': 22 (Invalid argument)')
        lto.skip_suffixes.append(" DER_NO_HDL(-1002): 'Invalid handle'")

    if ignore_busy:
        lto.skip_suffixes.append(" DER_BUSY(-1012): 'Device or resource busy'")

    def _issue_total():
        """Findings recorded so far, used to tell whether this log produced any."""
        total = len(conf.wf.issues) if conf.wf else 0
        if leak_wf is not None and leak_wf is not conf.wf:
            total += len(leak_wf.issues)
        return total

    issues_before = _issue_total()
    keep = getattr(conf.args, 'keep_logs', False)
    try:
        try:
            lto.check_log_file(abort_on_warning=True,
                               show_memleaks=show_memleaks,
                               leak_wf=leak_wf)
        except nlt_lt.LogCheckError:
            pass

        if skip_fi:
            if not lto.fi_triggered:
                raise NLTestNoFi

        if check_read or check_write or check_fstat:
            for line in log_iter.new_iter():
                if line.function != "ioil_show_summary":
                    continue
                print(line.get_msg())

                # These numbers match the D_INFO log line in the ioil_show_summary function.
                if check_read and int(line.get_field(3)) == 0:
                    raise NLTestIlZeroCall('read')

                if check_write and int(line.get_field(5)) == 0:
                    raise NLTestIlZeroCall('write')

                if check_fstat and int(line.get_field(8)) == 0:
                    raise NLTestIlZeroCall('fstat')

        if conf.max_log_size and fstat.st_size > conf.max_log_size:
            message = (f'Max log size exceeded, {sizeof_fmt(fstat.st_size)} > '
                       + sizeof_fmt(conf.max_log_size))
            conf.wf.add_test_case('logfile_size', failure=message)
    except Exception:  # pylint: disable=broad-exception-caught
        # Preserve the log on any error/abort path (fault not injected, IL check, etc.).
        keep = True
        raise
    finally:
        # Keep a log only if it produced a finding (or --keep-logs).  Fault-injection logs are
        # pruned by the fault-injection framework instead, once it knows if the run was of
        # interest, so leave those untouched here.
        if not defer_prune:
            if keep or _issue_total() > issues_before:
                conf.compress_file(filename)
            else:
                os.unlink(filename)

    return lto.fi_location

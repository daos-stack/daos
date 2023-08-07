#!/usr/bin/env python3
#
# Copyright 2018-2023 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""This provides consistency checking for CaRT log files."""

import re
import sys
import time
import argparse
from collections import OrderedDict, Counter

import cart_logparse
HAVE_TABULATE = True
try:
    import tabulate
except ImportError:
    HAVE_TABULATE = False

# pylint: disable=too-few-public-methods


class LogCheckError(Exception):
    """Error in the log parsing code"""

    def __str__(self):
        return self.__doc__


class NotAllFreed(LogCheckError):
    """Not all memory allocations freed"""


class WarningStrict(LogCheckError):
    """Error for warnings from strict files"""


class WarningMode(LogCheckError):
    """Error for warnings in strict mode"""


class ActiveDescriptors(LogCheckError):
    """Active descriptors at end of log file"""


class LogError(LogCheckError):
    """Errors detected in log file"""


class RegionContig():
    """Class to represent a memory region"""

    def __init__(self, start, end):
        self.start = start
        self.end = end

    def len(self):
        """Return the length of the region"""
        return self.end - self.start + 1

    def __str__(self):
        return '0x{:x}-0x{:x}'.format(self.start, self.end)

    def __eq__(self, other):
        if not isinstance(other, RegionContig):
            return False
        return self.start == other.start and self.end == other.end


def _ts_to_float(times):
    int_part = time.mktime(time.strptime(times[:-3], '%m/%d-%H:%M:%S'))
    float_part = int(times[-2:]) / 100
    return int_part + float_part


class RegionCounter():
    """Class to represent regions read/written to a file"""

    def __init__(self, start, end, times):
        self.start = start
        self.end = end
        self.reads = 1
        self.first_ts = times
        self.last_ts = times
        self.regions = []

    def add(self, start, end, times):
        """Record a new I/O operation"""
        self.reads += 1
        if start == self.end + 1:
            self.end = end
        else:
            self.regions.append(RegionContig(self.start, self.end))
            self.start = start
            self.end = end
        self.last_ts = times

    def __str__(self):
        bytes_count = 0

        # Make a list of the current regions, being careful not to
        # modify them.
        all_regions = list(self.regions)
        all_regions.append(RegionContig(self.start, self.end))

        regions = []
        prev_region = None
        rep_count = 0
        region = None
        for region in all_regions:
            bytes_count += region.len()
            if not prev_region or region == prev_region:
                rep_count += 1
                prev_region = region
                continue
            if rep_count > 0:
                regions.append('{}x{}'.format(str(region), rep_count))
            else:
                regions.append(str(region))
            rep_count = 1
            prev_region = region
        if rep_count > 0:
            regions.append('{}x{}'.format(str(region), rep_count))

        data = ','.join(regions)

        start_time = _ts_to_float(self.first_ts)
        end_time = _ts_to_float(self.last_ts)

        megabytes = int(bytes_count / (1024 * 1024))
        if megabytes * 1024 * 1024 == bytes_count:
            bytes_str = '{}Mb'.format(megabytes)
        else:
            bytes_str = '{:.1f}Mb'.format(bytes_count / (1024 * 1024))

        return '{} reads, {} {:.1f}Seconds {}'.format(self.reads,
                                                      bytes_str,
                                                      end_time - start_time,
                                                      data)


# Use a global variable here so show_line can remember previously reported
# error lines.
shown_logs = set()

# This is set by node_local_test in order to reconfigure this code so disable the invalid_name.
wf = None  # pylint: disable=invalid-name


def show_line(line, sev, msg, custom=None):
    """Output a log line in gcc error format"""
    # Only report each individual line once.

    log = "{}:{}:1: {}: {} '{}'".format(line.filename,
                                        line.lineno,
                                        sev,
                                        msg,
                                        line.get_anon_msg())
    if log in shown_logs:
        return False
    print(log)
    if custom:
        custom.add(line, sev, msg)
    elif wf:
        wf.add(line, sev, msg)
    shown_logs.add(log)
    return True


class HwmCounter():
    """Class to track integer values, with high-water mark"""

    # Used for memory allocation sizes currently.
    def __init__(self):
        self.__val = 0
        self.__hwm = 0
        self.count = 0
        self.__fcount = 0

    def has_data(self):
        """Return true if there is any data registered"""
        return self.__hwm != 0

    def __str__(self):
        return 'Total:{:,} HWM:{:,} {} allocations, '.format(self.__val,
                                                             self.__hwm,
                                                             self.count) + \
            '{} frees {} possible leaks'.format(self.__fcount, self.count - self.__fcount)

    def add(self, val):
        """Add a value"""
        self.count += 1
        if val < 0:
            return
        self.__val += val
        if self.__val > self.__hwm:
            self.__hwm = self.__val

    def subtract(self, val):
        """Subtract a value"""
        self.__fcount += 1
        if val < 0:
            return
        self.__val -= val


# During shutdown ERROR messages that end with these strings are not reported as errors.
SHUTDOWN_RC = ("DER_SHUTDOWN(-2017): 'Service should shut down'",
               "DER_NOTLEADER(-2008): 'Not service leader'")

# Functions that are never reported as errors.
IGNORED_FUNCTIONS = ('sched_watchdog_post', 'rdb_timerd')


class LogTest():
    """Log testing"""

    # pylint: disable=too-many-statements
    # pylint: disable=too-many-locals

    def __init__(self, log_iter, quiet=False):
        self.quiet = quiet
        self._li = log_iter
        self.hide_fi_calls = False
        self.fi_triggered = False
        self.fi_location = None
        self.skip_suffixes = []

        # Records on number, type and frequency of logging.
        self.log_locs = Counter()
        self.log_fac = Counter()
        self.log_levels = Counter()
        self.log_count = 0
        self._common_shown = False

    def __del__(self):
        if not self.quiet and not self._common_shown:
            self.show_common_logs()

    def save_log_line(self, line):
        """Record a single line of logging"""
        if self.quiet:
            return
        self.log_count += 1
        try:
            loc = '{}:{}'.format(line.filename, line.lineno)
        except AttributeError:
            loc = 'Unknown'
        self.log_locs[loc] += 1
        self.log_fac[line.fac] += 1
        self.log_levels[line.level] += 1

    def show_common_logs(self):
        """Report to stdout the most common logging locations"""
        if self.log_count == 0:
            return
        print('Parsed {} lines of logs'.format(self.log_count))
        print('Most common logging locations')
        for (loc, count) in self.log_locs.most_common(10):
            if count < 10:
                break
            percent = 100 * count / self.log_count
            print('Logging used {} times at {} ({:.1f}%)'.format(count, loc, percent))
        print('Most common facilities')
        for (fac, count) in self.log_fac.most_common(10):
            if count < 10:
                break
            print('{}: {} ({:.1f}%)'.format(fac, count, 100 * count / self.log_count))

        print('Most common levels')
        for (level, count) in self.log_levels.most_common(10):
            if count < 10:
                break
            print('{}: {} ({:.1f}%)'.format(cart_logparse.LOG_NAMES[level], count,
                                            100 * count / self.log_count))
        self._common_shown = True

    def check_log_file(self, abort_on_warning, show_memleaks=True, leak_wf=None):
        """Check a single log file for consistency"""
        to_raise = None
        for pid in self._li.get_pids():
            if wf:
                wf.reset_pending()
            try:
                self._check_pid_from_log_file(pid, abort_on_warning, leak_wf,
                                              show_memleaks=show_memleaks)
            except LogCheckError as error:
                if to_raise is None:
                    to_raise = error
        self.show_common_logs()
        if to_raise:
            raise to_raise

    def check_dfuse_io(self):
        """Parse dfuse i/o"""
        for pid in self._li.get_pids():

            client_pids = OrderedDict()
            for line in self._li.new_iter(pid=pid):
                self.save_log_line(line)
                if line.filename != 'src/client/dfuse/ops/read.c':
                    continue
                if line.get_field(3) != 'requested':
                    show_line(line, line.mask, "Extra output")
                    continue
                reg = line.re_region.fullmatch(line.get_field(2))
                start = int(reg.group(1), base=16)
                end = int(reg.group(2), base=16)
                reg = line.re_pid.fullmatch(line.get_field(4))

                pid = reg.group(1)

                if pid not in client_pids:
                    client_pids[pid] = RegionCounter(start, end, line.ts)
                else:
                    client_pids[pid].add(start, end, line.ts)

            for cpid in client_pids:
                print('{}:{}'.format(cpid, client_pids[pid]))

    def _check_pid_from_log_file(self, pid, abort_on_warning, leak_wf, show_memleaks=True):
        # pylint: disable=too-many-branches,too-many-nested-blocks
        """Check a pid from a single log file for consistency"""
        # Dict of active descriptors.
        active_desc = OrderedDict()
        active_desc['root'] = None

        # Dict of active RPCs
        active_rpcs = OrderedDict()

        fi_count = 0
        err_count = 0
        warnings_strict = False
        warnings_mode = False
        server_shutdown = False

        regions = OrderedDict()
        memsize = HwmCounter()

        old_regions = {}

        error_files = set()

        have_debug = False

        trace_lines = 0
        non_trace_lines = 0

        if self.quiet:
            rpc_r = None
        else:
            rpc_r = RpcReporting()

        for line in self._li.new_iter(pid=pid, stateful=True):
            if rpc_r:
                rpc_r.add_line(line)
            self.save_log_line(line)
            try:
                msg = ''.join(line._fields[2:])

                if 'DER_UNKNOWN' in msg:
                    show_line(line, 'NORMAL', 'Use of DER_UNKNOWN')
                # Warn if a line references the name of the function it was in,
                # but skip short function names or _internal suffixes.
                if line.function in msg and len(line.function) > 6 and \
                   re.search(r'\b' + line.function + r'\b', msg) is not None and \
                   '{}_internal'.format(line.function) not in msg:
                    show_line(line, 'NORMAL', 'Logging references function name')
            except AttributeError:
                pass
            if abort_on_warning:
                if not server_shutdown and \
                   line.fac != 'external' and line.function == 'server_fini':
                    server_shutdown = True
                if line.level <= cart_logparse.LOG_LEVELS['WARN']:
                    show = True
                    if self.hide_fi_calls and line.fac != 'external':
                        if line.is_fi_site():
                            show = False
                            fi_count += 1
                        elif line.is_fi_alloc_fail():
                            show = False
                            self.fi_triggered = True
                            self.fi_location = line
                            fi_count += 1
                        elif '-1009' in line.get_msg():

                            src_offset = line.lineno - self.fi_location.lineno
                            if line.filename == self.fi_location.filename:
                                src_offset = line.lineno
                                src_offset -= self.fi_location.lineno
                                if 0 < src_offset < 5:
                                    show_line(line, 'NORMAL',
                                              'Logging allocation failure')

                            if not line.get_msg().endswith("DER_NOMEM(-1009): 'Out of memory'"):
                                show_line(line, 'LOW', 'Error does not use DF_RC')
                            # For the fault injection test do not report
                            # errors for lines that print -DER_NOMEM, as
                            # this highlights other errors and lines which
                            # report an error, but not a fault code.
                            show = False
                        elif line.get_msg().endswith(': 12 (Cannot allocate memory)'):
                            # dfs and dfuse use system error numbers, rather
                            # than daos, so allow ENOMEM as well as
                            # -DER_NOMEM
                            show = False
                        elif line.get_msg().endswith(': 5 (HG_NOMEM)'):
                            # Mercury uses hg error numbers, rather
                            # than daos, so allow HG_NOMEM as well as
                            # -DER_NOMEM
                            show = False
                    elif line.rpc:
                        # Ignore the SWIM RPC opcode, as this often sends RPCs that fail during
                        # shutdown.
                        if line.rpc_opcode == '0xfe000000':
                            show = False
                    # Disable checking for a number of conditions, either because these errors/lines
                    # are badly formatted or because they're intermittent and we don't want noise in
                    # the test results.
                    if line.fac == 'external':
                        show = False
                    elif show and server_shutdown and any(map(line.get_msg().endswith,
                                                              SHUTDOWN_RC)):
                        show = False
                    elif show and line.function in IGNORED_FUNCTIONS:
                        show = False
                    if show and any(map(line.get_msg().endswith, self.skip_suffixes)):
                        show = False
                    if show:
                        # Allow WARNING or ERROR messages, but anything higher like assert should
                        # trigger a failure.
                        if line.level < cart_logparse.LOG_LEVELS['ERR']:
                            show_line(line, 'HIGH', 'error in strict mode')
                        else:
                            show_line(line, 'NORMAL', 'warning in strict mode')
                        warnings_mode = True
            if line.trace:
                trace_lines += 1
                if not have_debug and line.level > cart_logparse.LOG_LEVELS['INFO']:
                    have_debug = True
                desc = line.descriptor
                if line.is_new():
                    if desc in active_desc:
                        show_line(active_desc[desc], 'NORMAL', 'not deregistered')
                        show_line(line, 'NORMAL', 'already exists')
                        err_count += 1
                    if line.parent not in active_desc:
                        show_line(line, 'error', 'add with bad parent')
                        if line.parent in regions:
                            show_line(regions[line.parent], 'NORMAL',
                                      'used as parent without registering')
                        err_count += 1
                    active_desc[desc] = line
                elif line.is_link():
                    parent = line.parent
                    if parent not in active_desc:
                        show_line(line, 'NORMAL', 'link with bad parent')
                        err_count += 1
                    desc = parent
                elif line.is_new_rpc():
                    active_rpcs[line.descriptor] = line
                if line.is_dereg():
                    if desc in active_desc:
                        del active_desc[desc]
                    else:
                        show_line(line, 'NORMAL', 'invalid desc remove')
                        err_count += 1
                elif line.is_dereg_rpc():
                    if desc in active_rpcs:
                        del active_rpcs[desc]
                    else:
                        show_line(line, 'NORMAL', 'invalid rpc remove')
                        err_count += 1
                else:
                    if have_debug and desc not in active_desc and desc not in active_rpcs:
                        show_line(line, 'NORMAL', 'inactive desc')
                        if line.descriptor in regions:
                            show_line(regions[line.descriptor], 'NORMAL',
                                      'Used as descriptor without registering')
                        error_files.add(line.filename)
                        err_count += 1
            elif len(line._fields) > 2:
                # is_calloc() doesn't work on truncated output so only test if
                # there are more than two fields to work with.
                non_trace_lines += 1
                if line.is_calloc():
                    pointer = line.calloc_pointer()
                    if pointer in regions:
                        # Report both the old and new allocation points here.
                        show_line(regions[pointer], 'NORMAL',
                                  'new allocation seen for same pointer (old)')
                        show_line(line, 'NORMAL',
                                  'new allocation seen for same pointer (new)')
                        err_count += 1
                    regions[pointer] = line
                    memsize.add(line.calloc_size())
                elif line.is_free():
                    pointer = line.free_pointer()
                    # If a pointer is freed then automatically remove the
                    # descriptor
                    if pointer in active_desc:
                        del active_desc[pointer]
                    if pointer in regions:
                        memsize.subtract(regions[pointer].calloc_size())
                        old_regions[pointer] = [regions[pointer], line]
                        del regions[pointer]
                    elif pointer != '(nil)':
                        # Logs no longer contain free(NULL) however old logs might so continue
                        # to handle this case.
                        if pointer in old_regions:
                            if show_line(old_regions[pointer][0], 'ERROR',
                                         'double-free allocation point'):
                                print(f'Memory address is {pointer}')

                            show_line(old_regions[pointer][1], 'ERROR', '1st double-free location')
                            show_line(line, 'ERROR', '2nd double-free location')
                        else:
                            show_line(line, 'HIGH', 'free of unknown memory')
                        err_count += 1
                elif line.is_realloc():
                    (new_pointer, old_pointer) = line.realloc_pointers()
                    (new_size, old_size) = line.realloc_sizes()
                    if new_pointer != '(nil)' and old_pointer != '(nil)':
                        if old_pointer not in regions:
                            show_line(line, 'HIGH', 'realloc of unknown memory')
                        else:
                            # Use calloc_size() here as the memory might not
                            # come from a realloc() call.
                            exp_sz = regions[old_pointer].calloc_size()
                            if old_size not in (0, exp_sz, new_size):
                                show_line(line, 'HIGH', 'realloc used invalid old size')
                            memsize.subtract(exp_sz)
                    regions[new_pointer] = line
                    memsize.add(new_size)
                    if old_pointer not in (new_pointer, '(nil)'):
                        if old_pointer in regions:
                            old_regions[old_pointer] = [regions[old_pointer], line]
                            del regions[old_pointer]
                        else:
                            show_line(line, 'NORMAL', 'realloc of unknown memory')
                            err_count += 1

        del active_desc['root']
        if rpc_r:
            rpc_r.report()

        # This isn't currently used anyway.
        # if not have_debug:
        #    print('DEBUG not enabled, No log consistency checking possible')

        total_lines = trace_lines + non_trace_lines
        p_trace = trace_lines * 1.0 / total_lines * 100

        if not self.quiet:
            print("Pid {}, {} lines total, {} trace ({:.2f}%)".format(
                pid, total_lines, trace_lines, p_trace))
            if fi_count and memsize.count:
                print("Number of faults injected {} {:.2f}%".format(
                    fi_count, (fi_count / memsize.count) * 100))

        if memsize.has_data():
            print("Memsize: {}".format(memsize))

        # Special case the fuse arg values as these are allocated by IOF
        # but freed by fuse itself.
        # Skip over CaRT issues for now to get this landed, we can enable them
        # once this is stable.
        lost_memory = False
        if show_memleaks:
            for (_, line) in list(regions.items()):
                if line.is_calloc():
                    pointer = line.calloc_pointer()
                else:
                    assert line.is_realloc()
                    (pointer, _) = line.realloc_pointers()
                if pointer in active_desc:
                    if show_line(line, 'NORMAL', 'descriptor not freed', custom=leak_wf):
                        print(f'Memory address is {pointer}')
                    del active_desc[pointer]
                else:
                    if show_line(line, 'NORMAL', 'memory not freed', custom=leak_wf):
                        print(f'Memory address is {pointer}')
                lost_memory = True

        if active_desc:
            for (_, line) in list(active_desc.items()):
                show_line(line, 'NORMAL', 'desc not deregistered', custom=leak_wf)
            raise ActiveDescriptors()

        if active_rpcs:
            for (_, line) in list(active_rpcs.items()):
                show_line(line, 'NORMAL', 'rpc not deregistered')
        if error_files or err_count:
            raise LogError()
        if lost_memory:
            raise NotAllFreed()
        if warnings_strict:
            raise WarningStrict()
        if warnings_mode:
            raise WarningMode()


class RpcReporting():
    """Class for reporting a summary of RPC states"""

    known_functions = frozenset({'crt_hg_req_send',
                                 'crt_hg_req_destroy',
                                 'crt_rpc_complete',
                                 'crt_rpc_priv_alloc',
                                 'crt_rpc_handler_common',
                                 'crt_req_send',
                                 'crt_hg_req_send_cb'})

    def __init__(self):

        self._op_state_counters = {}
        self._c_states = {}
        self._c_state_names = set()
        self._current_opcodes = {}

    def add_line(self, line):
        """Parse a output line"""
        try:
            if line.function not in self.known_functions:
                return
        except AttributeError:
            return

        if line.is_new_rpc():
            rpc_state = 'ALLOCATED'
            opcode = line.get_field(-4)
            if opcode == 'per':
                opcode = line.get_field(-8)
        elif line.is_dereg_rpc():
            rpc_state = 'DEALLOCATED'
        elif line.endswith('submitted.'):
            rpc_state = 'SUBMITTED'
        elif line.function == 'crt_hg_req_send' and line.get_field(-6) == ('sent'):
            rpc_state = 'SENT'
        elif line.is_callback():
            rpc = line.descriptor
            rpc_state = 'COMPLETED'
            result = line.get_field(13).split('(')[0]
            self._c_state_names.add(result)
            opcode = self._current_opcodes[line.descriptor]
            try:
                self._c_states[opcode][result] += 1
            except KeyError:
                self._c_states[opcode] = Counter()
                self._c_states[opcode][result] += 1
        else:
            return

        rpc = line.descriptor

        if rpc_state == 'ALLOCATED':
            self._current_opcodes[rpc] = opcode
        else:
            opcode = self._current_opcodes[rpc]
        if rpc_state == 'DEALLOCATED':
            del self._current_opcodes[rpc]

        if opcode not in self._op_state_counters:
            self._op_state_counters[opcode] = {'ALLOCATED': 0,
                                               'DEALLOCATED': 0,
                                               'SENT': 0,
                                               'COMPLETED': 0,
                                               'SUBMITTED': 0}
        self._op_state_counters[opcode][rpc_state] += 1

    def report(self):
        """Print report to stdout"""
        if not bool(self._op_state_counters):
            return

        table = []
        errors = []
        names = sorted(self._c_state_names)
        if names:
            try:
                names.remove('DER_SUCCESS')
            except ValueError:
                pass
            names.insert(0, 'DER_SUCCESS')
        headers = ['OPCODE',
                   'ALLOCATED',
                   'SUBMITTED',
                   'SENT',
                   'COMPLETED',
                   'DEALLOCATED']

        for state in names:
            headers.append('-{}'.format(state))
        for (operation, counts) in sorted(self._op_state_counters.items()):
            row = [operation,
                   counts['ALLOCATED'],
                   counts['SUBMITTED'],
                   counts['SENT'],
                   counts['COMPLETED'],
                   counts['DEALLOCATED']]
            for state in names:
                try:
                    row.append(self._c_states[operation].get(state, ''))
                except KeyError:
                    row.append('')
            table.append(row)
            if counts['ALLOCATED'] != counts['DEALLOCATED']:
                errors.append("ERROR: Opcode {}: Alloc'd Total = {}, Dealloc'd Total = {}".
                              format(operation, counts['ALLOCATED'], counts['DEALLOCATED']))

        if HAVE_TABULATE:
            print('Opcode State Transition Tally')
            print(tabulate.tabulate(table,
                                    headers=headers,
                                    stralign='right'))

        for error in errors:
            print(error)


def run():
    """Trace a single file"""
    parser = argparse.ArgumentParser()
    parser.add_argument('--dfuse', help='Summarise dfuse I/O', action='store_true')
    parser.add_argument('--warnings', action='store_true')
    parser.add_argument('file', help='input file')
    args = parser.parse_args()
    try:
        log_iter = cart_logparse.LogIter(args.file)
    except UnicodeDecodeError:
        # If there is a unicode error in the log file then retry with checks
        # enabled which should both report the error and run in latin-1 so
        # perform the log parsing anyway.  The check for log_iter.file_corrupt
        # later on will ensure that this error does not get logged, then
        # ignored.
        # The only possible danger here is the file is simply too big to check
        # the encoding on, in which case this second attempt would fail with
        # an out-of-memory error.
        log_iter = cart_logparse.LogIter(args.file, check_encoding=True)
    test_iter = LogTest(log_iter)
    if args.dfuse:
        test_iter.check_dfuse_io()
    else:
        try:
            test_iter.check_log_file(args.warnings)
        except LogError:
            print('Errors in log file, ignoring')
        except NotAllFreed:
            print('Memory leaks, ignoring')
    if log_iter.file_corrupt:
        sys.exit(1)


if __name__ == '__main__':
    run()

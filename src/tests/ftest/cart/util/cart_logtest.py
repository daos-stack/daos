#!/usr/bin/env python3
# Copyright (C) 2018-2019 Intel Corporation
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
This provides consistency checking for CaRT log files.
"""

import sys
import time
import argparse
HAVE_TABULATE = True
try:
    import tabulate
except ImportError:
    HAVE_TABULATE = False
from collections import OrderedDict, Counter

import cart_logparse

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

def _ts_to_float(ts):
    int_part = time.mktime(time.strptime(ts[:-3], '%m/%d-%H:%M:%S'))
    float_part = int(ts[-2:])/100
    return int_part + float_part

class RegionCounter():
    """Class to represent regions read/written to a file"""
    def __init__(self, start, end, ts):
        self.start = start
        self.end = end
        self.reads = 1
        self.first_ts = ts
        self.last_ts = ts
        self.regions = []

    def add(self, start, end, ts):
        """Record a new I/O operation"""
        self.reads += 1
        if start == self.end + 1:
            self.end = end
        else:
            self.regions.append(RegionContig(self.start, self.end))
            self.start = start
            self.end = end
        self.last_ts = ts

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

        mb = int(bytes_count / (1024*1024))
        if mb * 1024 * 1024 == bytes_count:
            bytes_str = '{}Mb'.format(mb)
        else:
            bytes_str = '{:.1f}Mb'.format(bytes_count / (1024*1024))

        return '{} reads, {} {:.1f}Seconds {}'.format(self.reads,
                                                      bytes_str,
                                                      end_time - start_time,
                                                      data)

# Use a global variable here so show_line can remember previously reported
# error lines.
shown_logs = set()

# List of known areas where there may be a mismatch between the facility used
# for alloc vs free.  Typically this is where memory is allocated in one file
# but freed in another, however allocations in header file also feature.

# First is a lookup dict of commonly shared facilities, key is allocation
# facility, value is set of free facilities.

# Second part is a unordered dict of functions which are whitelisted
# specifically, key is function name, value is list of variables.
# Both the alloc and free function need to be whitelisted.

mismatch_table = {'container': ('common'),
                  'common': ('container', 'pool'),
                  'daos': ('common'),
                  'mgmt': ('common', 'daos', 'pool'),
                  'misc': ('common', 'mgmt'),
                  'pool': ('common'),
                  'server': ('daos')}

mismatch_alloc_ok = {'crt_self_uri_get': ('tmp_uri'),
                     'crt_rpc_handler_common': ('rpc_priv'),
                     'bio_sgl_init': ('sgl->bs_iovs'),
                     'pool_svc_name_cb': ('s'),
                     'daos_iov_copy': ('dst->iov_buf'),
                     'ds_pool_tgt_map_update': ('arg'),
                     'enum_cont_cb': ('ptr'),
                     'path_gen': ('*fpath'),
                     'd_sgl_init': ('sgl->sg_iovs'),
                     'iod_fetch': ('biovs')}

mismatch_free_ok = {'crt_rpc_priv_free': ('rpc_priv'),
                    'bio_sgl_fini': ('sgl->bs_iovs'),
                    'fini_free': ('svc->s_name',
                                  'svc->s_db_path'),
                    'd_sgl_fini': ('sgl->sg_iovs[i].iov_buf',
                                   'sgl->sg_iovs'),
                    'dtx_resync_ult': ('arg'),
                    'ds_pool_list_cont_handler': ('cont_buf'),
                    'notify_ready': ('req.uri')}

wf = None

def show_line(line, sev, msg):
    """Output a log line in gcc error format"""

    # Only report each individual line once.

    log = "{}:{}:1: {}: {} '{}'".format(line.filename,
                                        line.lineno,
                                        sev,
                                        msg,
                                        line.get_anon_msg())
    if log in shown_logs:
        return
    print(log)
    if wf:
        wf.add(line, sev, msg)
    shown_logs.add(log)

class hwm_counter():
    """Class to track integer values, with high-water mark"""

    # Used for memory allocation sizes currently.
    def __init__(self):
        self.__val = 0
        self.__hwm = 0
        self.__acount = 0
        self.__fcount = 0

    def has_data(self):
        """Return true if there is any data registered"""
        return self.__hwm != 0

    def __str__(self):
        return "Total:{:,} HWM:{:,} {} allocations, {} frees".\
            format(self.__val,
                   self.__hwm,
                   self.__acount,
                   self.__fcount)

    def add(self, val):
        """Add a value"""
        self.__acount += 1
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

#pylint: disable=too-many-statements
#pylint: disable=too-many-locals
class LogTest():
    """Log testing"""

    def __init__(self, log_iter):
        self._li = log_iter
        self.hide_fi_calls = False
        self.fi_triggered = False
        self.fi_location = None

        # Records on number, type and frequency of logging.
        self.log_locs = Counter()
        self.log_fac = Counter()
        self.log_levels = Counter()
        self.log_count = 0

    def __del__(self):
        self.show_common_logs()

    def save_log_line(self, line):
        """Record a single line of logging"""
        self.log_count += 1
        function = getattr(line, 'filename', None)
        if function:
            loc = '{}:{}'.format(line.filename, line.lineno)
        else:
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
            print('Logging used {} times at {} ({:.1f}%)'.format(count,
                                                                 loc,
                                                                 percent))
        print('Most common facilities')
        for (fac, count) in self.log_fac.most_common(10):
            if count < 10:
                break
            print('{}: {} ({:.1f}%)'.format(fac, count,
                                            100*count/self.log_count))

        print('Most common levels')
        for (level, count) in self.log_levels.most_common(10):
            if count < 10:
                break
            print('{}: {} ({:.1f}%)'.format(cart_logparse.LOG_NAMES[level],
                                            count,
                                            100*count/self.log_count))

    def check_log_file(self, abort_on_warning, show_memleaks=True):
        """Check a single log file for consistency"""

        for pid in self._li.get_pids():
            if wf:
                wf.reset_pending()
            self.rpc_reporting(pid)
            if wf:
                wf.reset_pending()
            self._check_pid_from_log_file(pid, abort_on_warning,
                                          show_memleaks=show_memleaks)

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

            for pid in client_pids:
                print('{}:{}'.format(pid, client_pids[pid]))

#pylint: disable=too-many-branches,too-many-nested-blocks
    def _check_pid_from_log_file(self, pid, abort_on_warning,
                                 show_memleaks=True):
        """Check a pid from a single log file for consistency"""

        # Dict of active descriptors.
        active_desc = OrderedDict()
        active_desc['root'] = None

        # Dict of active RPCs
        active_rpcs = OrderedDict()

        err_count = 0
        warnings_strict = False
        warnings_mode = False

        regions = OrderedDict()
        memsize = hwm_counter()

        old_regions = {}

        error_files = set()

        have_debug = False

        trace_lines = 0
        non_trace_lines = 0

        for line in self._li.new_iter(pid=pid, stateful=True):
            self.save_log_line(line)
            try:
                msg = ''.join(line._fields[2:])
                # Warn if a line references the name of the function it was in,
                # but skip short function names or _internal suffixes.
                if line.function in msg and len(line.function) > 6 and \
                   '{}_internal'.format(line.function) not in msg:
                    show_line(line, 'NORMAL',
                              'Logging references function name')
            except AttributeError:
                pass
            if abort_on_warning:
                if line.level <= cart_logparse.LOG_LEVELS['WARN']:
                    show = True
                    if self.hide_fi_calls:
                        if line.is_fi_site():
                            show = False
                            self.fi_triggered = True
                        elif line.is_fi_alloc_fail():
                            show = False
                            self.fi_location = line
                        elif '-1009' in line.get_msg():

                            src_offset = line.lineno - self.fi_location.lineno
                            if line.filename == self.fi_location.filename:
                                src_offset = line.lineno
                                src_offset -= self.fi_location.lineno
                                if src_offset > 0 and src_offset < 5:
                                    show_line(line, 'NORMAL',
                                              'Logging allocation failure')

                            if not line.get_msg().endswith(
                                    "DER_NOMEM(-1009): 'Out of memory'"):
                                show_line(line, 'LOW',
                                          'Error does not use DF_RC')
                            # For the fault injection test do not report
                            # errors for lines that print -DER_NOMEM, as
                            # this highlights other errors and lines which
                            # report an error, but not a fault code.
                            show = False
                    elif line.rpc:
                        # Ignore the SWIM RPC opcode, as this often sends RPCs
                        # that fail during shutdown.
                        if line.rpc_opcode == '0xfe000000':
                            show = False
                    if line.fac == 'external':
                        show = False
                    if show:
                        # Allow WARNING or ERROR messages, but anything higher
                        # like assert should trigger a failure.
                        if line.level < cart_logparse.LOG_LEVELS['ERR']:
                            show_line(line, 'HIGH', 'error in strict mode')
                        else:
                            show_line(line, 'NORMAL', 'warning in strict mode')
                        warnings_mode = True
            if line.trace:
                trace_lines += 1
                if not have_debug and \
                   line.level > cart_logparse.LOG_LEVELS['INFO']:
                    have_debug = True
                desc = line.descriptor
                if line.is_new():
                    if desc in active_desc:
                        show_line(active_desc[desc], 'NORMAL',
                                  'not deregistered')
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
                    if have_debug and desc not in active_desc and \
                       desc not in active_rpcs:

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
                    pointer = line.get_field(-1).rstrip('.')
                    if pointer in regions:
                        show_line(regions[pointer], 'NORMAL',
                                  'new allocation seen for same pointer')
                        err_count += 1
                    regions[pointer] = line
                    memsize.add(line.calloc_size())
                elif line.is_free():
                    pointer = line.get_field(-1).rstrip('.')
                    # If a pointer is freed then automatically remove the
                    # descriptor
                    if pointer in active_desc:
                        del active_desc[pointer]
                    if pointer in regions:
                        if line.fac != regions[pointer].fac:
                            fvar = line.get_field(3).strip("'")
                            afunc = regions[pointer].function
                            avar = regions[pointer].get_field(3).strip("':")
                            if line.function in mismatch_free_ok and \
                               fvar in mismatch_free_ok[line.function] and \
                               afunc in mismatch_alloc_ok and \
                               avar in mismatch_alloc_ok[afunc]:
                                pass
                            elif regions[pointer].fac in mismatch_table \
                                 and line.fac in  \
                                 mismatch_table[regions[pointer].fac]:
                                pass
                            else:
                                show_line(regions[pointer], 'LOW',
                                          'facility mismatch in alloc/free ' +
                                          '{} != {}'.format(
                                              regions[pointer].fac, line.fac))
                                show_line(line, 'LOW',
                                          'facility mismatch in alloc/free ' +
                                          '{} != {}'.format(
                                              regions[pointer].fac, line.fac))
                                err_count += 1
                        if line.level != regions[pointer].level:
                            show_line(regions[pointer], 'LOW',
                                      'level mismatch in alloc/free')
                            show_line(line, 'LOW',
                                      'level mismatch in alloc/free')
                            err_count += 1
                        memsize.subtract(regions[pointer].calloc_size())
                        old_regions[pointer] = [regions[pointer], line]
                        del regions[pointer]
                    elif pointer != '(nil)':
                        if pointer in old_regions:
                            show_line(old_regions[pointer][0], 'ERROR',
                                      'double-free allocation point')
                            show_line(old_regions[pointer][1], 'ERROR',
                                      '1st double-free location')
                            show_line(line, 'ERROR',
                                      '2nd double-free location')
                        else:
                            show_line(line, 'HIGH', 'free of unknown memory')
                        err_count += 1
                elif line.is_realloc():
                    new_pointer = line.get_field(-3)
                    old_pointer = line.get_field(-1)[:-2].split(':')[-1]
                    if new_pointer != '(nil)' and old_pointer != '(nil)':
                        memsize.subtract(regions[old_pointer].calloc_size())
                    regions[new_pointer] = line
                    memsize.add(line.calloc_size())
                    if old_pointer not in (new_pointer, '(nil)'):
                        if old_pointer in regions:
                            del regions[old_pointer]
                        else:
                            show_line(line, 'NORMAL',
                                      'realloc of unknown memory')
                            err_count += 1

        del active_desc['root']

        # This isn't currently used anyway.
        #if not have_debug:
        #    print('DEBUG not enabled, No log consistency checking possible')

        total_lines = trace_lines + non_trace_lines
        p_trace = trace_lines * 1.0 / total_lines * 100

        print("Pid {}, {} lines total, {} trace ({:.2f}%)".format(pid,
                                                                  total_lines,
                                                                  trace_lines,
                                                                  p_trace))

        if memsize.has_data():
            print("Memsize: {}".format(memsize))

        # Special case the fuse arg values as these are allocated by IOF
        # but freed by fuse itself.
        # Skip over CaRT issues for now to get this landed, we can enable them
        # once this is stable.
        lost_memory = False
        if show_memleaks:
            for (_, line) in regions.items():
                pointer = line.get_field(-1).rstrip('.')
                if pointer in active_desc:
                    show_line(line, 'NORMAL', 'descriptor not freed')
                    del active_desc[pointer]
                else:
                    show_line(line, 'NORMAL', 'memory not freed')
                lost_memory = True

        if active_desc:
            for (_, line) in active_desc.items():
                show_line(line, 'NORMAL', 'desc not deregistered')
            raise ActiveDescriptors()

        if active_rpcs:
            for (_, line) in active_rpcs.items():
                show_line(line, 'NORMAL', 'rpc not deregistered')
        if error_files or err_count:
            raise LogError()
        if lost_memory:
            raise NotAllFreed()
        if warnings_strict:
            raise WarningStrict()
        if warnings_mode:
            raise WarningMode()
#pylint: enable=too-many-branches,too-many-nested-blocks

    def rpc_reporting(self, pid):
        """RPC reporting for RPC state machine, for mutiprocesses"""
        op_state_counters = {}
        c_states = {}
        c_state_names = set()

        # Use to convert from descriptor to opcode.
        current_opcodes = {}

        for line in self._li.new_iter(pid=pid):
            rpc_state = None
            opcode = None

            function = getattr(line, 'function', None)
            if not function:
                continue
            if line.is_new_rpc():
                rpc_state = 'ALLOCATED'
                opcode = line.get_field(-4)
                if opcode == 'per':
                    opcode = line.get_field(-8)
            elif line.is_dereg_rpc():
                rpc_state = 'DEALLOCATED'
            elif line.endswith('submitted.'):
                rpc_state = 'SUBMITTED'
            elif function == 'crt_hg_req_send' and \
                 line.get_field(-6) == ('sent'):
                rpc_state = 'SENT'

            elif line.is_callback():
                rpc = line.descriptor
                rpc_state = 'COMPLETED'
                result = line.get_field(13).split('(')[0]
                c_state_names.add(result)
                opcode = current_opcodes[line.descriptor]
                try:
                    c_states[opcode][result] += 1
                except KeyError:

                    c_states[opcode] = Counter()
                    c_states[opcode][result] += 1
            else:
                continue

            rpc = line.descriptor

            if rpc_state == 'ALLOCATED':
                current_opcodes[rpc] = opcode
            else:
                opcode = current_opcodes[rpc]
            if rpc_state == 'DEALLOCATED':
                del current_opcodes[rpc]

            if opcode not in op_state_counters:
                op_state_counters[opcode] = {'ALLOCATED' :0,
                                             'DEALLOCATED': 0,
                                             'SENT':0,
                                             'COMPLETED':0,
                                             'SUBMITTED':0}
            op_state_counters[opcode][rpc_state] += 1

        if not bool(op_state_counters):
            return

        table = []
        errors = []
        names = sorted(c_state_names)
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
        for (op, counts) in sorted(op_state_counters.items()):
            row = [op,
                   counts['ALLOCATED'],
                   counts['SUBMITTED'],
                   counts['SENT'],
                   counts['COMPLETED'],
                   counts['DEALLOCATED']]
            for state in names:
                try:
                    row.append(c_states[op].get(state, ''))
                except KeyError:
                    row.append('')
            table.append(row)
            if counts['ALLOCATED'] != counts['DEALLOCATED']:
                errors.append("ERROR: Opcode {}: Alloc'd Total = {}, "
                              "Dealloc'd Total = {}". \
                              format(op,
                                     counts['ALLOCATED'],
                                     counts['DEALLOCATED']))

        if HAVE_TABULATE:
            print('Opcode State Transition Tally')
            print(tabulate.tabulate(table,
                                    headers=headers,
                                    stralign='right'))

        if errors:
            for error in errors:
                print(error)


def run():
    """Trace a single file"""
    parser = argparse.ArgumentParser()
    parser.add_argument('--dfuse',
                        help='Summarise dfuse I/O',
                        action='store_true')
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
            test_iter.check_log_file(False)
        except LogError:
            print('Errors in log file, ignoring')
        except NotAllFreed:
            print('Memory leaks, ignoring')
    if log_iter.file_corrupt:
        sys.exit(1)

if __name__ == '__main__':
    run()

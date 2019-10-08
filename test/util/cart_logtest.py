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

from collections import OrderedDict

import cart_logparse

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

WARN_FUNCTIONS = ['crt_grp_lc_addr_insert',
                  'crt_ctx_epi_abort',
                  'crt_rpc_complete',
                  'crt_req_timeout_hdlr',
                  'crt_req_hg_addr_lookup_cb',
                  'crt_progress',
                  'crt_context_timeout_check']

# Use a global variable here so show_line can remember previously reported
# error lines.
shown_logs = set()

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
    shown_logs.add(log)

def show_bug(line, bug_id):
    """Mark output with a known bug"""
    show_line(line, 'error', 'Known bug {}'.format(bug_id))

class hwm_counter():
    """Class to track integer values, with high-water mark"""

    # Used for memory allocation sizes currently.
    def __init__(self):
        self.__val = 0
        self.__hwm = 0
        self.__acount = 0
        self.__fcount = 0

    def __str__(self):
        return "Total:{:,} HWM:{:,} {} allocations, {} frees".\
            format(self.__val,
                   self.__hwm,
                   self.__acount,
                   self.__fcount)

    def add(self, val):
        """Add a value"""
        self.__val += val
        if self.__val > self.__hwm:
            self.__hwm = self.__val
        self.__acount += 1

    def subtract(self, val):
        """Subtract a value"""
        self.__val -= val
        self.__fcount += 1

#pylint: disable=too-many-statements
#pylint: disable=too-many-locals
#pylint: disable=too-few-public-methods
class LogTest():
    """Log testing"""

    def __init__(self, log_iter):
        self._li = log_iter
        self.strict_functions = {}
        self.error_files_ok = set()

    def set_warning_function(self, function, bug_id):
        """Register functions for known bugs

        Add a mapping from functions with errors to known bugs
        """
        self.strict_functions[function] = bug_id

    def set_error_ok(self, file_name):
        """Register a file as having known errors"""
        self.error_files_ok.add(file_name)

    def check_log_file(self, abort_on_warning):
        """Check a single log file for consistency"""

        for pid in self._li.get_pids():
            self._check_pid_from_log_file(pid, abort_on_warning)

#pylint: disable=too-many-branches,no-self-use,too-many-nested-blocks
    def _check_pid_from_log_file(self, pid, abort_on_warning):
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

        error_files = set()

        # List of known free locations where there may be a mismatch, this is a
        # dict of functions, each with a unordered list of variables that are
        # freed by the function.
        # Typically this is where memory is allocated in one file, and freed in
        # another.
        mismatch_free_ok = {'crt_plugin_fini': ('timeout_cb_priv',
                                                'cb_priv',
                                                'prog_cb_priv',
                                                'event_cb_priv'),
                            'crt_finalize': ('crt_gdata.cg_addr'),
                            'crt_rpc_priv_free': ('rpc_priv'),
                            'crt_init_opt': ('crt_gdata.cg_addr')}

        have_debug = False

        trace_lines = 0
        non_trace_lines = 0

        for line in self._li.new_iter(pid=pid, stateful=True):
            try:
                # Not all log lines contain a function so catch that case
                # here and do not abort.
                if line.function in self.strict_functions and \
                   line.level <= cart_logparse.LOG_LEVELS['WARN']:
                    show_line(line, 'error', 'warning in strict file')
                    show_bug(line, self.strict_functions[line.function])
                    warnings_strict = True
            except AttributeError:
                pass
            if abort_on_warning:
                if line.level <= cart_logparse.LOG_LEVELS['WARN'] and \
                   line.mask.lower() != 'hg' and \
                   line.function not in WARN_FUNCTIONS:
                    if line.rpc:
                        # Ignore the SWIM RPC opcode, as this often sends RPCs
                        # that fail during shutdown.
                        if line.rpc_opcode != '0xfe000000':
                            show_line(line, 'error', 'warning in strict mode')
                            warnings_mode = True
                    else:
                        show_line(line, 'error', 'warning in strict mode')
                        warnings_mode = True
            if line.trace:
                trace_lines += 1
                if not have_debug and \
                   line.level > cart_logparse.LOG_LEVELS['INFO']:
                    have_debug = True
                desc = line.descriptor
                if line.is_new():
                    if desc in active_desc:
                        show_line(line, 'error', 'already exists')
                        show_line(active_desc[desc], 'error',
                                  'not deregistered')
                        err_count += 1
                    if line.parent not in active_desc:
                        show_line(line, 'error', 'add with bad parent')
                        err_count += 1
                    active_desc[desc] = line
                elif line.is_link():
                    parent = line.parent
                    if parent not in active_desc:
                        show_line(line, 'error', 'link with bad parent')
                        err_count += 1
                    desc = parent
                elif line.is_new_rpc():
                    active_rpcs[line.descriptor] = line
                if line.is_dereg():
                    if desc in active_desc:
                        del active_desc[desc]
                    else:
                        show_line(line, 'error', 'invalid desc remove')
                        err_count += 1
                elif line.is_dereg_rpc():
                    if desc in active_rpcs:
                        del active_rpcs[desc]
                    else:
                        show_line(line, 'error', 'invalid rpc remove')
                        err_count += 1
                else:
                    if desc not in active_desc and \
                       desc not in active_rpcs and \
                       have_debug and line.filename not in self.error_files_ok:

                        # There's something about this particular function
                        # that makes it very slow at logging output.
                        show_line(line, 'error', 'inactive desc')
                        error_files.add(line.filename)
                        err_count += 1
            else:
                non_trace_lines += 1
                if line.is_calloc():
                    pointer = line.get_field(-1).rstrip('.')
                    regions[pointer] = line
                    memsize.add(line.calloc_size())
                elif line.is_free():
                    pointer = line.get_field(-1).rstrip('.')
                    if pointer in regions:
                        if line.mask != regions[pointer].mask:
                            var = line.get_field(3).strip("'")
                            if line.function in mismatch_free_ok and \
                               var in mismatch_free_ok[line.function]:
                                pass
                            else:
                                show_line(regions[pointer], 'warning',
                                          'mask mismatch in alloc/free')
                                show_line(line, 'warning',
                                          'mask mismatch in alloc/free')
                        if line.level != regions[pointer].level:
                            show_line(regions[pointer], 'warning',
                                      'level mismatch in alloc/free')
                            show_line(line, 'warning',
                                      'level mismatch in alloc/free')
                        memsize.subtract(regions[pointer].calloc_size())
                        del regions[pointer]
                    elif pointer != '(nil)':
                        show_line(line, 'error', 'free of unknown memory')
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
                            show_line(line, 'error',
                                      'realloc of unknown memory')

        del active_desc['root']

        if not have_debug:
            print('DEBUG not enabled, No log consistency checking possible')

        total_lines = trace_lines + non_trace_lines
        p_trace = trace_lines * 1.0 / total_lines * 100

        print("Pid {}, {} lines total, {} trace ({:.2f}%)".format(pid,
                                                                  total_lines,
                                                                  trace_lines,
                                                                  p_trace))

        print("Memsize: {}".format(memsize))

        # Special case the fuse arg values as these are allocated by IOF
        # but freed by fuse itself.
        # Skip over CaRT issues for now to get this landed, we can enable them
        # once this is stable.
        lost_memory = False
        for (_, line) in regions.items():
            if line.function == 'initialize_projection':
                continue
            show_line(line, 'error', 'memory not freed')
            lost_memory = True

        if active_desc:
            for (_, line) in active_desc.items():
                show_line(line, 'error', 'desc not deregistered')
            raise ActiveDescriptors()

        if active_rpcs:
            for (_, line) in active_rpcs.items():
                show_line(line, 'error', 'rpc not deregistered')
        if error_files or err_count:
            raise LogError()
        if lost_memory:
            raise NotAllFreed()
        if warnings_strict:
            raise WarningStrict()
        if warnings_mode:
            raise WarningMode()
#pylint: enable=too-many-branches,no-self-use,too-many-nested-blocks

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
import pprint
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

# Use a global variable here so show_line can remember previously reported
# error lines.
shown_logs = set()

# List of known locations where there may be a mismatch, this is a
# dict of functions, each with a unordered list of variables that are
# freed by the function.
# Typically this is where memory is allocated in one file, and freed in
# another.
mismatch_alloc_ok = {'crt_self_uri_get': ('tmp_uri'),
                     'crt_rpc_handler_common': ('rpc_priv'),
                     'crt_proc_d_iov_t': ('div->iov_buf'),
                     'grp_add_to_membs_list': ('tmp'),
                     'grp_regen_linear_list': ('tmp_ptr'),
                     'crt_proc_d_string_t': ('*data'),
                     'crt_hg_init': ('*addr'),
                     'crt_proc_d_rank_list_t': ('rank_list',
                                                'rank_list->rl_ranks'),
                     'path_gen': ('*fpath'),
                     'ds_pool_tgt_map_update': ('arg'),
                     'get_attach_info': ('reqb'),
                     'iod_fetch': ('biovs'),
                     'bio_sgl_init': ('sgl->bs_iovs'),
                     'process_credential_response': ('bytes'),
                     'pool_map_find_tgts': ('*tgt_pp'),
                     'daos_acl_dup': ('acl_copy'),
                     'dfuse_pool_lookup': ('ie', 'dfs', 'dfp'),
                     'pool_prop_read': ('prop->dpp_entries[idx].dpe_str',
                                        'prop->dpp_entries[idx].dpe_val_ptr'),
                     'cont_prop_read': ('prop->dpp_entries[idx].dpe_str',
                                        'prop->dpp_entries[idx].dpe_val_ptr'),
                     'cont_prop_default_copy': ('entry_def->dpe_str'),
                     'cont_iv_prop_g2l': ('prop_entry->dpe_str'),
                     'enum_cont_cb': ('ptr'),
                     'obj_enum_prep_sgls': ('dst_sgls[i].sg_iovs',
                                            'dst_sgls[i].sg_iovs[j].iov_buf'),
                     'notify_ready': ('reqb'),
                     'pool_svc_name_cb': ('s'),
                     'local_name_to_principal_name': ('*name'),
                     'pack_daos_response': ('body'),
                     'ds_mgmt_drpc_get_attach_info': ('body'),
                     'mgmt_svc_locate_cb': ('s'),
                     'mgmt_svc_name_cb': ('s'),
                     'pool_prop_default_copy': ('entry_def->dpe_str'),
                     'pool_iv_prop_g2l': ('prop_entry->dpe_str'),
                     'daos_prop_entry_copy': ('entry_dup->dpe_str'),
                     'daos_prop_dup': ('entry_dup->dpe_str'),
                     'auth_cred_to_iov': ('packed')}

mismatch_free_ok = {'crt_finalize': ('crt_gdata.cg_addr'),
                    'crt_group_psr_set': ('uri'),
                    'crt_hdlr_uri_lookup': ('tmp_uri'),
                    'crt_rpc_priv_free': ('rpc_priv'),
                    'crt_init_opt': ('crt_gdata.cg_addr'),
                    'cont_prop_default_copy': ('entry_def->dpe_str'),
                    'ds_pool_list_cont_handler': ('cont_buf'),
                    'dtx_resync_ult': ('arg'),
                    'fini_free': ('svc->s_name',
                                  'svc->s_db_path'),
                    'daos_sgl_fini': ('sgl->sg_iovs[i].iov_buf',
                                      'sgl->sg_iovs'),
                    'd_rank_list_free': ('rank_list',
                                         'rank_list->rl_ranks'),
                    'pool_prop_default_copy': ('entry_def->dpe_str'),
                    'pool_svc_store_uuid_cb': ('path'),
                    'ds_mgmt_svc_start': ('uri'),
                    'ds_rsvc_lookup': ('path'),
                    'daos_acl_free': ('acl'),
                    'drpc_free': ('pointer'),
                    'pool_child_add_one': ('path'),
                    'bio_sgl_fini': ('sgl->bs_iovs'),
                    'daos_iov_free': ('iov->iov_buf'),
                    'daos_prop_entry_free_value': ('entry->dpe_str',
                                                   'entry->dpe_val_ptr'),
                    'main': ('dfs'),
                    'start_one': ('path'),
                    'pool_svc_load_uuid_cb': ('path'),
                    'ie_sclose': ('ie', 'dfs', 'dfp'),
                    'notify_ready': ('req.uri'),
                    'get_tgt_rank': ('tgts')}

memleak_ok = ['dfuse_start',
              'expand_vector',
              'd_rank_list_alloc',
              'get_tpv',
              'get_new_entry',
              'get_attach_info',
              'drpc_call_create']

EFILES = ['src/common/misc.c',
          'src/common/prop.c',
          'src/cart/crt_hg_proc.c',
          'src/security/cli_security.c',
          'src/client/dfuse/dfuse_core.c']

mismatch_alloc_seen = {}
mismatch_free_seen = {}

output_file = None

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
    if output_file:
        output_file.write("{}\n".format(log))
        output_file.flush()
    shown_logs.add(log)

def add_line_count_to_dict(line, target):
    """Add entry for a output line into a dict"""

    # This is used for keeping tabs on how many allocations/frees there
    # have been.
    if line.function not in target:
        target[line.function] = {}
    var = line.get_field(3).strip("':")
    if var not in target[line.function]:
        target[line.function][var] = 0
    target[line.function][var] += 1

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
#pylint: disable=too-few-public-methods
class LogTest():
    """Log testing"""

    def __init__(self, log_iter):
        self._li = log_iter
        self.hide_fi_calls = False
        self.fi_triggered = False
        self.fi_location = None

    def check_log_file(self, abort_on_warning, show_memleaks=True):
        """Check a single log file for consistency"""

        for pid in self._li.get_pids():
            self._check_pid_from_log_file(pid, abort_on_warning,
                                          show_memleaks=show_memleaks)

#pylint: disable=too-many-branches,no-self-use,too-many-nested-blocks
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
                    if show:
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
                        if line.parent in regions:
                            show_line(regions[line.parent], 'warning',
                                      'used as parent without registering')
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
                       have_debug and line.filename not in EFILES:

                        # There's something about this particular function
                        # that makes it very slow at logging output.
                        show_line(line, 'error', 'inactive desc')
                        if line.descriptor in regions:
                            show_line(regions[line.descriptor], 'error',
                                      'Used as descriptor without registering')
                        error_files.add(line.filename)
                        err_count += 1
            else:
                non_trace_lines += 1
                if line.is_calloc():
                    pointer = line.get_field(-1).rstrip('.')
                    if pointer in regions:
                        show_line(regions[pointer], 'error',
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
                        if line.mask != regions[pointer].mask:
                            fvar = line.get_field(3).strip("'")
                            afunc = regions[pointer].function
                            avar = regions[pointer].get_field(3).strip("':")
                            if line.function in mismatch_free_ok and \
                               fvar in mismatch_free_ok[line.function] and \
                               afunc in mismatch_alloc_ok and \
                               avar in mismatch_alloc_ok[afunc]:
                                pass
                            else:
                                show_line(regions[pointer], 'warning',
                                          'mask mismatch in alloc/free')
                                show_line(line, 'warning',
                                          'mask mismatch in alloc/free')
                                err_count += 1
                            add_line_count_to_dict(line, mismatch_free_seen)
                            add_line_count_to_dict(regions[pointer],
                                                   mismatch_alloc_seen)
                        if line.level != regions[pointer].level:
                            show_line(regions[pointer], 'warning',
                                      'level mismatch in alloc/free')
                            show_line(line, 'warning',
                                      'level mismatch in alloc/free')
                            err_count += 1
                        memsize.subtract(regions[pointer].calloc_size())
                        old_regions[pointer] = [regions[pointer], line]
                        del regions[pointer]
                    elif pointer != '(nil)':
                        if pointer in old_regions:
                            show_line(old_regions[pointer][0], 'error', 'double-free allocation point')
                            show_line(old_regions[pointer][1], 'error', '1st double-free location')
                            show_line(line, 'error', '2nd double-free location')
                        else:
                            show_line(line, 'error', 'free of unknown memory')
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
                            show_line(line, 'error',
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

        print("Memsize: {}".format(memsize))

        if False:
            pp = pprint.PrettyPrinter()
            if mismatch_alloc_seen:
                print('Mismatched allocations were allocated here:')
                print(pp.pformat(mismatch_alloc_seen))
            if mismatch_free_seen:
                print('Mismatched allocations were freed here:')
                print(pp.pformat(mismatch_free_seen))

        if not show_memleaks:
            return

        # Special case the fuse arg values as these are allocated by IOF
        # but freed by fuse itself.
        # Skip over CaRT issues for now to get this landed, we can enable them
        # once this is stable.
        lost_memory = False
        for (_, line) in regions.items():
            if line.function in memleak_ok:
                continue
            pointer = line.get_field(-1).rstrip('.')
            if pointer in active_desc:
                show_line(line, 'error', 'descriptor not freed')
                del active_desc[pointer]
            else:
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

def trace_one_file(filename):
    """Trace a single file"""
    log_iter = cart_logparse.LogIter(filename)
    test_iter = LogTest(log_iter)
    test_iter.check_log_file(True)

if __name__ == '__main__':
    if len(sys.argv) == 2:
        trace_one_file(sys.argv[1])

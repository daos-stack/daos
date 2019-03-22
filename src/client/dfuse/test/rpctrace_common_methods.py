#!/usr/bin/env python3
# Copyright (C) 2017-2019 Intel Corporation
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
RpcTrace class definition
Methods associated to RPC tracing for error debuging.
"""

import os
from collections import OrderedDict
import common_methods
import iof_cart_logtest
import iof_cart_logparse

# CaRT Error numbers to convert to strings.
C_ERRNOS = {0: '-DER_SUCCESS',
            -1006: 'DER_UNREACH',
            -1011: '-DER_TIMEDOUT',
            -1032: '-DER_EVICTED'}

# pylint: disable=too-many-locals
# pylint: disable=too-many-statements
# pylint: disable=too-many-branches
# pylint: disable=too-many-instance-attributes
class RpcTrace(common_methods.ColorizedOutput):
    """RPC tracing methods
    RPC = CaRT Priv
    Descriptor = handle/iof_pol_type/iof_pool/iof_projection_info/iof_state/
    cnss_plugin/plugin_entry/cnss_info"""

    ALLOC_STATE = 'ALLOCATED'
    DEALLOC_STATE = 'DEALLOCATED'
    SUBMIT_STATE = 'SUBMITTED'
    SENT_STATE = 'SENT'
    COMPLETED_STATE = 'COMPLETED'

    # A list of allowed descriptor state transitions, the key is the new
    # state, the value is a list of previous states which are expected.
    STATE_CHANGE_TABLE = {ALLOC_STATE: (DEALLOC_STATE),
                          DEALLOC_STATE: (ALLOC_STATE,
                                          SUBMIT_STATE,
                                          SENT_STATE,
                                          COMPLETED_STATE),
                          SUBMIT_STATE: (ALLOC_STATE),
                          SENT_STATE: (SUBMIT_STATE,
                                       COMPLETED_STATE),
                          COMPLETED_STATE: (SENT_STATE,
                                            SUBMIT_STATE,
                                            ALLOC_STATE)}

    VERBOSE_STATE_TRANSITIONS = False
    VERBOSE_LOG = True

    def __init__(self, log_iter, output_stream):
        self.set_log(output_stream)
        self.input_file = log_iter.fname

        #index_multiprocess will determine which PID to trace
        #(ie index 0 will be assigned the first PID found in logs)

        self._lf = log_iter
        self.pids = self._lf.get_pids()

    def _rpc_error_state_tracing(self, rpc_dict, rpc, rpc_state):
        """Error checking for rpc state"""

        # Returns a tuple of (State, Extra string)
        status = None
        message = None
        if rpc in rpc_dict:
            if rpc_dict[rpc] in self.STATE_CHANGE_TABLE[rpc_state]:
                status = 'SUCCESS'
            else:
                status = 'ERROR'
                message = 'previous state: {}'.format(rpc_dict[rpc])

        elif rpc_state == self.ALLOC_STATE:
            status = 'SUCCESS'
        else:
            status = 'WARN'
            message = "no alloc'd state registered"

        if rpc_state == self.DEALLOC_STATE:
            del rpc_dict[rpc]
        else:
            rpc_dict[rpc] = rpc_state
        return (status, message)

    def rpc_reporting(self, pid):
        """RPC reporting for RPC state machine, for mutiprocesses"""
        rpc_dict = {}
        op_state_counters = {}
        c_states = {}
        c_state_names = set()

        # Use to convert from descriptor to opcode.
        current_opcodes = {}

        self.normal_output('CaRT RPC Reporting:\nLogfile: {}, '
                           'PID: {}\n'.format(self.input_file,
                                              pid))

        output_table = []

        for line in self._lf.new_iter(pid=pid):
            rpc_state = None
            opcode = None

            if line.is_new_rpc():
                rpc_state = self.ALLOC_STATE
                opcode = line.get_field(3)
            elif line.is_dereg_rpc():
                rpc_state = self.DEALLOC_STATE
            elif line.endswith('submitted.'):
                rpc_state = self.SUBMIT_STATE
            elif line.endswith(' sent.'):
                rpc_state = self.SENT_STATE
            elif line.is_callback():
                rpc = line.descriptor
                rpc_state = self.COMPLETED_STATE
                result = line.get_field(-1).rstrip('.')
                result = C_ERRNOS.get(int(result), result)
                c_state_names.add(result)
                opcode = current_opcodes[line.descriptor]
                try:
                    c_states[opcode][result] += 1
                except KeyError:
                    try:
                        c_states[opcode][result] = 1
                    except KeyError:
                        c_states[opcode] = {}
                        c_states[opcode][result] = 1
            else:
                continue

            rpc = line.descriptor

            if rpc_state == self.ALLOC_STATE:
                current_opcodes[rpc] = opcode
            else:
                opcode = current_opcodes[rpc]
            if rpc_state == self.DEALLOC_STATE:
                del current_opcodes[rpc]

            if opcode not in op_state_counters:
                op_state_counters[opcode] = {self.ALLOC_STATE :0,
                                             self.DEALLOC_STATE: 0,
                                             self.SENT_STATE:0,
                                             self.COMPLETED_STATE:0,
                                             self.SUBMIT_STATE:0}
            op_state_counters[opcode][rpc_state] += 1

            (state, extra) = self._rpc_error_state_tracing(rpc_dict,
                                                           rpc,
                                                           rpc_state)

            if self.VERBOSE_STATE_TRANSITIONS or state != 'SUCCESS':
                output_table.append([state,
                                     rpc,
                                     rpc_state,
                                     opcode,
                                     line.function,
                                     extra])

        if output_table:
            self.table_output(output_table,
                              title='RPC State Transitions:',
                              headers=['STATE',
                                       'RPC',
                                       'STATE',
                                       'Op',
                                       'Function',
                                       'Extra'])

        table = []
        errors = []
        names = sorted(c_state_names)
        if names:
            try:
                names.remove('-DER_SUCCESS')
            except ValueError:
                pass
            names.insert(0, '-DER_SUCCESS')
        headers = ['OPCODE',
                   self.ALLOC_STATE,
                   self.SUBMIT_STATE,
                   self.SENT_STATE,
                   self.COMPLETED_STATE,
                   self.DEALLOC_STATE]

        for state in names:
            headers.append(state)
        for (op, counts) in sorted(op_state_counters.items()):
            row = [op,
                   counts[self.ALLOC_STATE],
                   counts[self.SUBMIT_STATE],
                   counts[self.SENT_STATE],
                   counts[self.COMPLETED_STATE],
                   counts[self.DEALLOC_STATE]]
            for state in names:
                try:
                    row.append(c_states[op].get(state, ''))
                except KeyError:
                    row.append('')
            table.append(row)
            if counts[self.ALLOC_STATE] != counts[self.DEALLOC_STATE]:
                errors.append("ERROR: Opcode {}: Alloc'd Total = {}, "
                              "Dealloc'd Total = {}". \
                              format(op,
                                     counts[self.ALLOC_STATE],
                                     counts[self.DEALLOC_STATE]))
            # Sent can be more than completed because of corpcs but shouldn't
            # be less
            if counts[self.SENT_STATE] > counts[self.COMPLETED_STATE]:
                errors.append("ERROR: Opcode {}: sent Total = {}, "
                              "Completed Total = {}". \
                              format(op,
                                     counts[self.SENT_STATE],
                                     counts[self.COMPLETED_STATE]))

        self.table_output(table,
                          title='Opcode State Transition Tally',
                          headers=headers,
                          stralign='right')

        if errors:
            self.list_output(errors)

    #************ Descriptor Tracing Methods (IOF_TRACE macros) **********

    def _descriptor_error_state_tracing(self, pid):
        """Check for any descriptors that are not registered/deregistered"""

        desc_state = OrderedDict()
        # Maintain a list of parent->children relationships.  This is a dict of
        # sets, using the parent as the key and the value is a set of children.
        # When a descriptor is deleted then all child RPCs in the set are also
        # removed from desc_state
        linked = {}

        output_table = []

        for line in self._lf.new_iter(pid=pid, trace_only=True):
            state = None
            desc = line.descriptor
            if desc == '':
                continue

            msg = None
            is_error = True

            if line.is_new():
                state = 'Registered'
                if desc in desc_state:
                    msg = 'previous state: {}'.format(desc_state[desc])
                else:
                    is_error = False
                desc_state[desc] = state
                linked[desc] = set()
            if line.is_link():
                state = 'Linked'
                if desc in desc_state:
                    msg = 'Already linked'
                else:
                    is_error = False
                desc_state[desc] = state
                parent = line.parent
                if parent in linked:
                    linked[parent].add(desc)
            elif line.is_dereg():
                state = 'Deregistered'
                if desc not in desc_state:
                    msg = 'Not registered'
                elif desc_state.get(desc, None) == 'Registered':
                    del desc_state[desc]
                    is_error = False
                else:
                    msg = desc_state[desc]
                if desc in linked:
                    for child in linked[desc]:
                        if child in desc_state:
                            del desc_state[child]
                    del linked[desc]
            else:
                continue

            if is_error:
                self.have_errors = True

            if not self.VERBOSE_STATE_TRANSITIONS and not is_error:
                continue

            if is_error:
                output_table.append([desc,
                                     '{} (Error)'.format(state),
                                     line.function,
                                     msg,
                                     '{}: {}'.format(line.index,
                                                     line.get_msg())])
            else:
                output_table.append([desc, state, line.function, None, None])

        if output_table:
            self.table_output(output_table,
                              title='Descriptor State Transitions:',
                              headers=['Descriptor',
                                       'State',
                                       'Function',
                                       'Message',
                                       'Line'])

        #check if all descriptors are deregistered
        for d, state in desc_state.items():
            if state == 'Registered':
                self.error_output('{} is not Deregistered'.format(d))
            else:
                self.error_output('{}:{} not Deregistered from state'.\
                                  format(d, state))

    def descriptor_rpc_trace(self, pid):
        """Parses twice thru log to create a hierarchy of descriptors and also
           a dict storing all RPCs tied to a descriptor"""

        # Dictionary, time ordered containing a mapping of descriptors, and a
        # array of (pointer, type) RPC entries for each descriptor.
        desc_table = OrderedDict()
        desc_dict = {}
        self.normal_output('IOF Descriptor/RPC Tracing:\n'
                           'Logfile: {}'.format(self.input_file))

        # Indexed by pub.
        rpc_table = {}

        rpc_pub2priv = {}

        self._descriptor_error_state_tracing(pid)

        fuse_file = os.path.join('src', 'ioc', 'ops')
        to_trace = None
        to_trace_fuse = None

        line = None
        for line in self._lf.new_iter(pid=pid, stateful=True):

            # Make a note of the first descriptor with a warning message
            # so this can be selected later for tracing.
            if line.trace and not to_trace and \
               (line.level <= iof_cart_logparse.LOG_LEVELS['WARN']):
                to_trace = line

            if line.is_new():

                # Make a note of the first fuse descriptor encountered to be
                # traced if there are no warning messages in the file.
                if not to_trace and \
                   not to_trace_fuse and line.filename.startswith(fuse_file):
                    to_trace_fuse = line

                obj_type = line.get_field(-3)[1:-1]

                desc_table[line.pdesc] = (obj_type, line.pparent)
                desc_dict[line.pdesc] = []

            elif line.is_link():
                # register RPCs tied to given handle
                # Link lines are a little odd in the log file in that they are
                # the wrong way round, line.parent is the descriptor and
                # line.descriptor is the new RPC.

                desc = line.pparent

                if desc in desc_dict:
                    if line.pdesc in rpc_pub2priv:
                        rpc = rpc_pub2priv[line.pdesc]
                    else:
                        rpc = line.pdesc
                    desc_dict[desc].append((rpc,
                                            line.get_field(-3)[1:-1]))
                else:
                    self.error_output('Descriptor {} is not present'.\
                                      format(desc))

            elif line.is_new_rpc():
                rpc = line.descriptor
                # This next line depends a output format change to CaRT
                # logging.
                if line.get_field(4) == 'rpc_pub:':
                    rpc_pub = line.get_field(5).rstrip(')')
                    rpc_pub2priv[rpc_pub] = rpc
                    rpc_table[rpc] = {'pub' : rpc_pub}
            elif line.is_dereg_rpc():
                rpc = line.descriptor
                if rpc in rpc_table:
                    rpc_pub = rpc_table[rpc]['pub']
                    del rpc_table[rpc]
                    del rpc_pub2priv[rpc_pub]

        # Check for ticket 884.  This happens occasionally in testing but we do
        # not have a reliable reproducer, so detect if it happens and log about
        # it.
        if line and line.function == 'ioc_fuse_destroy':
            iof_cart_logtest.show_bug(line, 'IOF-884')

        if not to_trace and not to_trace_fuse:
            return None

        return {'to_trace': to_trace,
                'to_trace_fuse': to_trace_fuse,
                'desc_table' : desc_table,
                'desc_dict' : desc_dict,
                'pid': pid}

    def _rpc_trace_output_logdump(self, t_desc):
        """Prints all log messages relating to the given descriptor or any
           pointer in the descriptor's hierarchy"""

        pid = t_desc['pid']
        desc_table = t_desc['desc_table']
        desc_dict = t_desc['desc_dict']
        if t_desc['to_trace']:
            descriptor = t_desc['to_trace']
        else:
            descriptor = t_desc['to_trace_fuse']

        trace = descriptor.pdesc
        #append an extra line
        output = []
        traces_for_log_dump = []
        output.append('')
        if not descriptor.rpc:
            try:
                while trace != "root":
                    traces_for_log_dump.append(trace)
                    (trace_type, parent) = desc_table[trace]
                    output.append('{}: {}'.format(trace_type, trace))
                    for (desc, name) in desc_dict[trace]:
                        traces_for_log_dump.append(desc)
                        output.append('\t{} {}'.format(name, desc))
                    trace = parent
            except KeyError:
                self.error_output('Descriptor {} does not trace back to root'\
                                  .format(descriptor.pdesc))

        traces_for_log_dump.append(trace)

        if not output:
            self.error_output('Descriptor {} not currently registered or '
                              'linked'.format(descriptor.pdesc))
        else:
            output.insert(0, '\nDescriptor Hierarchy ({}):'\
                          .format(descriptor.pdesc))
            self.list_output(output)

        self.log_output('\nLog dump for descriptor hierarchy ({}):'\
                        .format(traces_for_log_dump[0]))

        for line in self._lf.new_iter(pid=pid, raw=True, stateful=True):

            if line.trace and line.pdesc in traces_for_log_dump:
                self.log_output(line.to_str(mark=True))
            elif line.is_link() and line.pparent in traces_for_log_dump:
                self.log_output(line.to_str(mark=True))
            elif self.VERBOSE_LOG:
                # Only show INFO and below if the descriptor is not marked for
                # display.  This dramatically reduces the volume of output.
                if line.level < iof_cart_logparse.LOG_LEVELS['DBUG']:
                    self.log_output(line.to_str())

    def rpc_trace_output(self, t_desc):
        """Dumps all RPCs tied to a descriptor, descriptor hierarchy, and all
           log messages related to descriptor"""
        missing_links = []
        output_table = []

        desc_table = t_desc['desc_table']
        desc_dict = t_desc['desc_dict']
        if t_desc['to_trace']:
            descriptor = t_desc['to_trace']
            self.normal_output('Tracing descriptor {} with error/warning'. \
                               format(descriptor.pdesc))
        else:
            descriptor = t_desc['to_trace_fuse']

        for key in desc_table:
            (stype, par) = desc_table[key]

            rpcs = []
            if key in desc_dict:
                for i in desc_dict[key]:
                    rpcs.append('{} {}'.format(i[0], i[1]))

            if par in desc_table:
                (par_type, _) = desc_table[par]
                parent_string = "{} {}".format(par, par_type)
            elif par == 'root':
                parent_string = par

            else: #fail if missing parent/link
                parent_string = "{} [None]".format(par)
                missing_links.append(par)

            output_table.append([key,
                                 stype,
                                 '\n'.join(rpcs),
                                 parent_string])

        self.normal_output('')
        self.table_output(output_table,
                          headers=['Descriptor',
                                   'Type',
                                   'RPCs',
                                   'Parent'])

        #Log dump for descriptor hierarchy
        self._rpc_trace_output_logdump(t_desc)

        return missing_links

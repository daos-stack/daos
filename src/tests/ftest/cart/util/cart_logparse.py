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
LogIter class definition.
LogLine class definition.

This provides a way of querying CaRT logfiles for processing.
"""

from collections import OrderedDict
import bz2
import os
import re

class InvalidPid(Exception):
    """Exception to be raised when invalid pid is requested"""
    pass

class InvalidLogFile(Exception):
    """Exception to be raised when log file cannot be parsed"""
    pass

LOG_LEVELS = {'FATAL' :1,
              'EMRG'  :2,
              'CRIT'  :3,
              'ERR'   :4,
              'WARN'  :5,
              'NOTE'  :6,
              'INFO'  :7,
              'DBUG'  :8}

# Make a reverse lookup from log level to name.
LOG_NAMES = {}
for name in LOG_LEVELS:
    LOG_NAMES[LOG_LEVELS[name]] = name

# pylint: disable=too-few-public-methods
class LogRaw():
    """Class for raw (non cart log lines) in cart log files

    This is used for lines that cannot be identified as cart log lines,
    for example mercury logs being sent to the same file.
    """
    def __init__(self, line):
        self.line = line.rstrip('\n')
        self.trace = False

    def to_str(self):
        """Convert the object to a string, in a way that is compatible with
        LogLine
        """
        return self.line

# pylint: disable=too-many-instance-attributes
class LogLine():
    """Class for parsing CaRT log lines

    This class implements a way of inspecting individual lines of a log
    file.

    It allows for queries such as 'string in line' which will match against
    the message only, and != which will match the entire line.
    """

    # Match an address range, a region in memory.
    re_region = re.compile(r"(0|0x[0-9a-f]{1,16})-(0x[0-9a-f]{1,16})")
    # Match a pointer, with optional ) . or , suffix.
    re_pointer = re.compile(r"0x[0-9a-f]{1,16}((\)|\.|\,)?)")
    # Match a pid marker
    re_pid = re.compile(r"pid=(\d+)")

    # Match a truncated uuid from DF_UUID
    re_uuid = re.compile(r"[0-9a-f]{8}(:?)")
    # Match a truncated uuid[rank] from DF_DB
    re_uuid_rank = re.compile(r"[0-9,a-f]{8}\[\d+\](:?)")
    # Match from DF_UIOD
    re_uiod = re.compile(r"\d{1,20}\.\d{1,20}.(\d{1,10})")
    # Match a RPCID from RPC_TRACE macro.
    re_rpcid = re.compile(r"rpcid=0x[0-9a-f]{1,16}")

    def __init__(self, line):
        fields = line.split()
        # Work out the end of the fixed-width portion, and the beginning of the
        # message.  The hostname and pid fields are both variable width
        idx = 29 + len(fields[1]) + len(fields[2])
        pidtid = fields[2][5:-1]
        pid = pidtid.split("/")
        self.pid = int(pid[0])
        self._preamble = line[:idx]
        self.fac = fields[3]
        try:
            self.level = LOG_LEVELS[fields[4]]
        except KeyError:
            raise InvalidLogFile(fields[4])

        self.ts = fields[0]
        self._fields = fields[5:]
        try:
            if self._fields[1][-2:] == '()':
                self.trace = False
                self.function = self._fields[1][:-2]
            elif self._fields[1][-1:] == ')':
                self.trace = True
            else:
                self.trace = False
        except IndexError:
            # Catch truncated log lines.
            self.trace = False

        if self.trace:
            if self.level == 7 or self.level == 3:
                if self.fac == 'rpc' or self.fac == 'hg':
                    del self._fields[2:5]

        if self.trace:
            fn_str = self._fields[1]
            start_idx = fn_str.find('(')
            self.function = fn_str[:start_idx]
            desc = fn_str[start_idx+1:-1]
            if desc == '(nil)':
                self.descriptor = ''
            else:
                self.descriptor = desc
        self._msg = ' '.join(self._fields)

    def to_str(self, mark=False):
        """Convert the object to a string"""
        pre = self._preamble.split(' ', 3)
        preamble = ' '.join([pre[0], pre[3]])
        if mark:
            return '{} ** {}'.format(preamble, self._msg)
        return '{}    {}'.format(preamble, self._msg)

    def __getattr__(self, attr):
        if attr == 'parent':
            if self._fields[2] == 'Registered':
                # This is a bit of a hack but handle the case where descriptor
                # names contain spaces.
                if self._fields[6] == 'from':
                    return self._fields[7]
                return self._fields[6]
            if self._fields[2] == 'Link':
                return self._fields[5]
        if attr == 'filename':
            try:
                (filename, _) = self._fields[0].split(':')
                return filename
            except ValueError:
                pass
        elif attr == 'lineno':
            try:
                (_, lineno) = self._fields[0].split(':')
                return int(lineno)
            except ValueError:
                pass
        raise AttributeError

    def get_msg(self):
        """Return the message part of a line, stripping up to and
        including the filename"""
        return ' '.join(self._fields[1:])

    def get_anon_msg(self):
        """Return the message part of a line, stripping up to and
        including the filename but removing pointers"""

        # As get_msg, but try and remove specific information from the message,
        # This is so that large volumes of logs can be amalgamated and reduced
        # a common set for easier reporting.  Specifically the trace pointer,
        # fid/revision of GAH values and other pointers are removed.
        #
        # These can then be fed back as source-level comments to the source-code
        # without creating too much output.

        fields = []
        for entry in self._fields[2:]:
            field = None

            r = self.re_region.fullmatch(entry)
            if r:
                field = '0x...-0x...'

            if not field:
                r = self.re_pointer.fullmatch(entry)
                if r:
                    field = '0x...{}'.format(r.group(1))
            if not field:
                r = self.re_pid.fullmatch(entry)
                if r:
                    field = 'pid=<pid>'
            if not field:
                r = self.re_uuid.fullmatch(entry)
                if r:
                    field = 'uuid{}'.format(r.group(1))
            if not field:
                r = self.re_uuid_rank.fullmatch(entry)
                if r:
                    field = 'uuid/rank{}'.format(r.group(1))
            if not field:
                r = self.re_uiod.fullmatch(entry)
                if r:
                    field = 'uoid.{}'.format(r.group(1))
            if not field:
                r = self.re_rpcid.fullmatch(entry)
                if r:
                    field = 'rpcid=<rpcid>'
            if field:
                fields.append(field)
            else:
                fields.append(entry)

        return '{}() {}'.format(self.function, ' '.join(fields))

    def endswith(self, item):
        """Mimic the str.endswith() function

        This only matches on the actual string part of the message, not the
        timestamp/pid/faculty parts.
        """
        return self._msg.endswith(item)

    def get_field(self, idx):
        """Return a specific field from the line"""
        return self._fields[idx]

    def _is_type(self, text, trace=True, base=2):
        """Checks for text in a log message

        Returns True if the line starts with the text provided
        """
        if trace and not self.trace:
            return False

        # Check that the contents of two arrays are equal, using text as is and
        # selecting only the correct entries of the fields array.
        return text == self._fields[base:base+len(text)]

    def is_new(self):
        """Returns True if line is new descriptor"""

        return self._is_type(['Registered', 'new'])

    def is_dereg(self):
        """Returns true if line is descriptor deregister"""

        return self._is_type(['Deregistered'])

    def is_new_rpc(self):
        """Returns True if line is new rpc"""

        if not self.trace:
            return False

        if self._fields[-1] == 'allocated.':
            return True

        if self._fields[-1] == 'received.' and self._fields[-5] == 'allocated':
            return True

        return False

    def is_dereg_rpc(self):
        """Returns true if line is a rpc deregister"""

        if not self.trace:
            return False
        if self.function != 'crt_hg_req_destroy':
            return False

        return self._fields[-1] == 'destroying'

    def is_callback(self):
        """Returns true if line is RPC callback"""

        if self.function not in ('crt_hg_req_send_cb', 'crt_rpc_complete'):
            return False

        return self._is_type(['Invoking', 'RPC', 'callback'], base=5)

    def is_link(self):
        """Returns True if line is Link descriptor"""

        return self._is_type(['Link'])

    def is_fi_site(self):
        """Return True if line is record of fault injection"""
        return self._is_type(['fault_id'], trace=False)

    def is_fi_alloc_fail(self):
        """Return True if line is showing failed memory allocation"""
        return self._is_type(['out', 'of', 'memory'], trace=False)

    def is_calloc(self):
        """Returns True if line is a allocation point"""
        return self.get_field(2).startswith('alloc(')

    def is_realloc(self):
        """Returns True if line is a call to"""
        return self.get_field(2) == 'realloc'

    def calloc_size(self):
        """Returns the size of the allocation"""
        if self.get_field(5) == '*':
            if self.is_realloc():
                field = -5
            else:
                field = -3
            count = int(self.get_field(field).split(':')[-1])
            return count * int(self.get_field(4))
        return int(self.get_field(4))

    def is_free(self):
        """Returns True if line is a call to free"""
        return self.get_field(2) == 'free'


# pylint: disable=too-many-branches
class StateIter():
    """Helper class for LogIter to add a statefull iterator.

    Implement a new iterator() for LogIter() that tracks descriptors
    and adds two new attributes, pdesc and pparent which are the local
    descriptor with the reuse-count appended.
    """
    def __init__(self, li):
        self.reuse_table = {}
        self.active_desc = {}
        self.li = li
        self._l = None

    def __iter__(self):

        # Dict, indexed by pointer, containing re-use index for that pointer.
        self.reuse_table = {}
        # Conversion from active pointer to line where it was created.
        self.active_desc = {}

        self._l = iter(self.li)
        return self

    def __next__(self):
        line = next(self._l)

        if not line.trace:
            line.rpc = False
            return line

        if line.is_new() or line.is_new_rpc():
            if line.descriptor in self.reuse_table:
                self.reuse_table[line.descriptor] += 1
                line.pdesc = '{}_{}'.format(line.descriptor,
                                            self.reuse_table[line.descriptor])
            else:
                self.reuse_table[line.descriptor] = 0
                line.pdesc = line.descriptor
            self.active_desc[line.descriptor] = line
            if line.is_new():
                if line.parent in self.active_desc:
                    line.pparent = self.active_desc[line.parent].pdesc
                else:
                    line.pparent = line.parent
                line.rpc = False
            else:
                line.rpc = True
        elif line.is_link():
            if line.parent in self.active_desc:
                line.pparent = self.active_desc[line.parent].pdesc
            else:
                line.pparent = line.parent
            line.pdesc = line.descriptor
            line.rpc = False
        else:
            if line.descriptor in self.active_desc:
                line.rpc = self.active_desc[line.descriptor].rpc
                if not line.rpc:
                    line.pparent = self.active_desc[line.descriptor].pparent
                line.pdesc = self.active_desc[line.descriptor].pdesc
                line.rpc_opcode = self.active_desc[line.descriptor].get_field(3)
            else:
                line.pdesc = line.descriptor
                line.rpc = False

            if (line.is_dereg() or line.is_dereg_rpc()) and \
               line.descriptor in self.active_desc:
                del self.active_desc[line.descriptor]

        return line

    def next(self):
        """Python2/3 compat function"""
        return self.__next__()

# pylint: disable=too-many-branches

# pylint: disable=too-few-public-methods

class LogIter():
    """Class for parsing CaRT log files

    This class implements a iterator for lines in a cart log file.  The iterator
    is rewindable, and there are options for automatically skipping lines.
    """

    def __init__(self, fname, check_encoding=False):
        """Load a file, and check how many processes have written to it"""

        # Depending on file size either pre-read entire file into memory,
        # or do a first pass checking the pid list.  This allows the same
        # iterator to work fast if the file can be kept in memory, or the
        # same, bug slower if it needs to be re-read each time.
        #
        # Try and open the file as utf-8, but if that doesn't work then
        # find and report the error, then continue with the file open as
        # latin-1
        self._fd = None

        self.file_corrupt = False

        self.bz2 = False

        # Force check encoding for smaller files.
        i = os.stat(fname)
        if i.st_size < (1024*1024*20):
            check_encoding = True

        if fname.endswith('.bz2'):
            # Allow direct operation on bz2 files.  Supports multiple pids
            # per file as normal, however does not try and seek to file
            # positions, rather walks the entire file for each pid.
            self._fd = bz2.open(fname, 'rt')
            self.bz2 = True
        else:
            if check_encoding:
                try:
                    self._fd = open(fname, 'r', encoding='utf-8')
                    self._fd.read()
                except UnicodeDecodeError as err:
                    print('ERROR: Invalid data in server.log on following line')
                    self._fd = open(fname, 'r', encoding='latin-1')
                    self._fd.read(err.start - 200)
                    data = self._fd.read(199)
                    lines = data.splitlines()
                    print(lines[-1])
                    self.file_corrupt = True

                    # This will now work, as the file has been opened in
                    # latin-1 rather than unicode.
                self._fd.seek(0)
            else:
                self._fd = open(fname, 'r', encoding='utf-8')

        self.fname = fname
        self._data = []

        i = os.fstat(self._fd.fileno())
        self.__from_file = bool(i.st_size > (1024*1024*20)) or self.bz2

        if self.__from_file:
            self._load_pids()
        else:
            self._load_data()

        # Offset into the file when iterating.  This is an array index, and is
        # based from zero, as opposed to line index which is based from 1.
        self._offset = 0

        self._pid = None
        self._trace_only = False
        self._raw = False
        self._iter_index = 0
        self._iter_count = 0
        self._iter_pid = None
        self._iter_last_index = 0

    def _load_data(self):
        """Load all data into memory"""

        pids = OrderedDict()

        index = 0
        for line in self._fd:
            fields = line.split(' ', 8)
            index += 1
            l_pid = None
            if len(fields) < 6 or len(fields[0]) != 17 or fields[0][2] != '/':
                self._data.append(LogRaw(line))
            else:
                l_obj = LogLine(line)
                l_pid = l_obj.pid
                self._data.append(l_obj)
                if l_pid in pids:
                    pids[l_pid]['line_count'] += 1
                else:
                    pids[l_pid] = {'line_count': 1,
                                   'first_index': index}
                pids[l_pid]['last_index'] = index
        self._pids = pids

    def _load_pids(self):
        """Iterate through the file, loading data on pids"""

        pids = OrderedDict()

        index = 0
        position = 0
        for line in self._fd:
            fields = line.split(' ', 8)
            index += 1
            l_pid = None
            if len(fields) < 6 or len(fields[0]) != 17:
                position += len(line)
                continue
            pidtid = fields[2][5:-1]
            pid = pidtid.split("/")
            l_pid = int(pid[0])
            if l_pid in pids:
                pids[l_pid]['line_count'] += 1
            else:
                pids[l_pid] = {'line_count': 1,
                               'file_pos': position,
                               'first_index': index}
            pids[l_pid]['last_index'] = index
            position += len(line)
        self._pids = pids

    def new_iter(self,
                 pid=None,
                 stateful=False,
                 trace_only=False,
                 raw=False):
        """Rewind file iterator, and set options

        If pid is set the the iterator will only return lines matching the pid
        If trace_only is True then the iterator will only return trace lines.
        if raw is set then all lines in the file are returned, even non-log
        lines.
        """

        if pid is not None:
            try:
                self._iter_pid = self._pids[pid]
            except KeyError:
                raise InvalidPid

            if self.__from_file:
                if self.bz2:
                    self._iter_last_index = self._iter_pid['last_index']
                else:
                    self._iter_last_index = self._iter_pid['last_index'] - \
                                            self._iter_pid['first_index'] + 1
            else:
                self._iter_last_index = self._iter_pid['last_index']

            self._pid = pid
        else:
            self._pid = None
            self._iter_pid = None
            self._iter_last_index = 0
        self._trace_only = trace_only
        self._raw = raw

        if stateful:
            if not pid:
                raise InvalidPid
            return StateIter(self)

        return self

    def __iter__(self):
        self._iter_index = 0
        self._iter_count = 0
        if self.__from_file:
            if not self._pid or self.bz2:
                self._fd.seek(0)
            else:
                self._fd.seek(self._iter_pid['file_pos'])
        else:
            self._offset = 0
        return self

    def __lnext(self):
        """Helper function for __next__"""

        if self.__from_file:
            line = self._fd.readline()
            if not line:
                raise StopIteration
            fields = line.split(' ', 8)
            if len(fields) < 6 or len(fields[0]) != 17:
                return LogRaw(line)
            return LogLine(line)

        try:
            line = self._data[self._offset]
        except IndexError:
            raise StopIteration
        self._offset += 1
        return line

    def __next__(self):

        while True:
            self._iter_index += 1

            if self._pid and self._iter_index > self._iter_last_index:
                assert self._iter_count == self._iter_pid['line_count'] # nosec
                raise StopIteration

            line = self.__lnext()

            if not self._raw and isinstance(line, LogRaw):
                continue

            if self._trace_only and not line.trace:
                continue

            if self._pid:
                if line.pid != self._pid:
                    continue

                if isinstance(line, LogRaw):
                    continue

            self._iter_count += 1
            return line

    def next(self):
        """Python2/3 compat function"""
        return self.__next__()

    def get_pids(self):
        """Return an array of pids appearing in the file"""
        return self._pids.keys()
# pylint: enable=too-many-instance-attributes

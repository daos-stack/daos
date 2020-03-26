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

import os

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

    index is the line in the file, starting at 1.
    """
    def __init__(self, line, index):
        fields = line.split()
        # Work out the end of the fixed-width portion, and the beginning of the
        # message.  The hostname and pid fields are both variable width
        idx = 29 + len(fields[1]) + len(fields[2])
        pidtid = fields[2][5:-1]
        pid = pidtid.split("/")
        self.pid = int(pid[0])
        self._preamble = line[:idx]
        self.index = index
        self.mask = fields[3]
        try:
            self.level = LOG_LEVELS[fields[4]]
        except KeyError:
            raise InvalidLogFile(fields[4])

        self._fields = fields[5:]
        if self._fields[1][-2:] == '()':
            self.trace = False
            self.function = self._fields[1][:-2]
        elif self._fields[1][-1:] == ')':
            self.trace = True
        else:
            self.trace = False

        if self.trace:
            if self.level == 7 or self.level == 3:
                if self.mask == 'rpc' or self.mask == 'hg':
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
#        pre = self._preamble.split(' ', maxsplit=3)
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
            if entry.startswith('Gah('):
                (root, _, _) = entry[4:-1].split('.')
                fields.append('Gah({}.-.-)'.format(root))
            elif entry.startswith('0x') and len(entry) > 5:
                if entry.endswith(')'):
                    fields.append('0x...)')
                else:
                    fields.append('0x...')
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

    def _is_type(self, text, trace=True):
        """Checks for text in a log message

        Retuns True if the line starts with the text provided
        """
        if trace and not self.trace:
            return False

        # Check that the contents of two arrays are equal, using text as is and
        # selecting only the correct entries of the fields array.
        return text == self._fields[2:2+len(text)]

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

        # TODO: This is broken for now but the RPCtrace has not been ported yet
        # so there are no current users of it.

        return self._is_type(['Invoking', 'RPC', 'callback'])

    def is_link(self):
        """Returns True if line is Link descriptor"""

        return self._is_type(['Link'])

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
        return self.__next__()

# pylint: disable=too-many-branches

# pylint: disable=too-few-public-methods

class LogIter():
    """Class for parsing CaRT log files

    This class implements a iterator for lines in a cart log file.  The iterator
    is rewindable, and there are options for automatically skipping lines.
    """

    def __init__(self, fname):
        """Load a file, and check how many processes have written to it"""

        # Depending on file size either pre-read entire file into memory,
        # or do a first pass checking the pid list.  This allows the same
        # iterator to work fast if the file can be kept in memory, or the
        # same, bug slower if it needs to be re-read each time.
        #
        # Try and open the file as utf-8, but if that doesn't work then
        # find and report the error, then continue with the file open as
        # latin-1
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

        self._fd.seek(0)
        self.fname = fname
        self._data = []
        index = 0
        pids = set()

        i = os.fstat(self._fd.fileno())
        self.__from_file = bool(i.st_size > (1024*1024*20))
        self.__index = 0

        for line in self._fd:
 #           fields = line.split(maxsplit=8)
            fields = line.split(' ', 8)
            index += 1
            if self.__from_file:
                if len(fields) < 6 or len(fields[0]) != 17:
                    continue
                l_obj = LogLine(line, index)
                pids.add(l_obj.pid)
            else:
                if len(fields) < 6 or len(fields[0]) != 17:
                    self._data.append(LogRaw(line))
                else:
                    l_obj = LogLine(line, index)
                    pids.add(l_obj.pid)
                    self._data.append(l_obj)

        # Offset into the file when iterating.  This is an array index, and is
        # based from zero, as opposed to line index which is based from 1.
        self._offset = 0

        self._pid = None
        self._trace_only = False
        self._raw = False
        self._pids = sorted(pids)

    def __del__(self):
        if self._fd:
            self._fd.close()

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
            if pid not in self._pids:
                raise InvalidPid
            self._pid = pid
        else:
            self._pid = None
        self._trace_only = trace_only
        self._raw = raw

        if stateful:
            if not pid:
                raise InvalidPid
            return StateIter(self)

        return self

    def __iter__(self, pid=None):
        if self.__from_file:
            self._fd.seek(0)
            self.__index = 0
        else:
            self._offset = 0
        return self

    def __lnext(self):
        """Helper function for __next__"""

        if self.__from_file:
            line = self._fd.readline()
            if not line:
                raise StopIteration
            self.__index += 1
 #           fields = line.split(maxsplit=8)
            fields = line.split(' ', 8)
            if len(fields) < 6 or len(fields[0]) != 17:
                return LogRaw(line)
            return LogLine(line, self.__index)

        try:
            line = self._data[self._offset]
        except IndexError:
            raise StopIteration
        self._offset += 1
        return line

    def __next__(self):

        while True:
            line = self.__lnext()

            if not self._raw and isinstance(line, LogRaw):
                continue

            if self._trace_only and not line.trace:
                continue

            if isinstance(line, LogRaw) and self._pid:
                continue

            if self._pid and line.pid != self._pid:
                continue

            return line

    def next(self):
        return self.__next__()

    def get_pids(self):
        """Return an array of pids appearing in the file"""
        return self._pids
# pylint: enable=too-many-instance-attributes

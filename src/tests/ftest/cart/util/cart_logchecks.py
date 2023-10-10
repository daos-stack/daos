# (C) Copyright 2023 Intel Corporation
#
# SPDX-License-Identifier: BSD-2-Clause-Patent

"""LogTest Plugin for reporting relationships between descriptors"""

import pprint


class DLogEntity(dict):
    """A log entity from DAOS logging"""

    def __init__(self, line):  # pylint: disable=unused-argument
        self.quiet = False
        self.logging_functions = set()
        self.deleted = False
        self.children = []
        self['type'] = self.name()

    def mark_line(self, line):
        """Record a line"""
        self.logging_functions.add(line.function)
        self['functions'] = self.logging_functions

    def stats(self):
        """Return some description about this entity"""
        fns = ','.join(sorted(self.logging_functions))
        return '  functions used: {}, {} children'.format(fns, len(self.children))

    def delete(self):
        """Mark an item as deleted"""
        self.quiet = True
        self.deleted = True

    def add_child(self, child):
        """Add a child object"""
        self.children.append(child)
        if 'children' not in self:
            self['children'] = []

        self['children'].append(child)

    def name(self):
        """Return a name for this type"""
        return type(self).__name__


class DirHandle(DLogEntity):
    """Directory handle"""

    def __init__(self, line):
        super().__init__(line)
        self.desc = line.descriptor
        self.parent = line.parent
        self.readdir_count = 0
        self.plus_count = 0
        self.entries = 0

    def __str__(self):
        txt = 'Directory handle, id {} parent {} calls {} with {} entries,'.format(
            self.desc, self.parent, self.readdir_count, self.entries)
        if self.readdir_count == 1 and self.plus_count == 1:
            txt += ' Only one readdir call'
        elif self.plus_count == 1:
            txt += ' Adaptive readdirplus'
        elif self.plus_count == self.readdir_count:
            txt += ' Readdirplus used'
        return txt

    def add_line(self, line):
        """Record a line for the handle"""
        if line.get_field(2) == 'plus':
            self.readdir_count += 1
            if line.get_field(3) == '1':
                self.plus_count += 1
        elif line.get_field(2) == 'Checking':
            pass
        elif line.get_field(2) == 'Replying':
            self.entries += int(line.get_field(4))


class ReaddirHandle(DLogEntity):
    """Readdir handle"""

    def __init__(self, line):
        super().__init__(line)
        self.line = line
        self.parent = line.parent
        self.max_ref = 1

    def __str__(self):
        return 'Readdir handle,   id {} max_ref {}'.format(self.line.descriptor, self.max_ref)

    def add_line(self, line):
        """Record a line for the handle"""
        if line.get_field(2) in ('Appending', 'Adding', 'Added', 'Fetching'):
            return
        if line.get_field(2) == 'Ref':
            ref = int(line.get_field(4).strip(','))
            if ref > self.max_ref:
                self.max_ref = ref
            return


class InodeHandle(DLogEntity):
    """Inode handle"""

    def __init__(self, line):
        super().__init__(line)
        self.line = line
        self.dentry = None
        self.parent = line.parent
        self.getattr_calls = 0
        self.size = 0

    def __str__(self):
        if self.dentry is None:
            return 'inode, no dentry'
        return "inode: {} name '{}' getattr {} size {}".format(self.line.descriptor, self.dentry,
                                                               self.getattr_calls, self.size)

    def add_line(self, line):
        """Record a line for the handle"""
        if self.dentry is not None:
            if line.function == 'dfuse_cb_getattr' and line.get_field(2) == 'Returning':
                self.getattr_calls += 1
                self.size = int(line.get_field(9))
                return
            return

        if line.get_field(2) == 'file':
            self.dentry = line.get_field(3)[3:-1]
            self['name'] = self.dentry
            return


class FileHandle(DLogEntity):
    """File handle"""

    def __init__(self, line):
        super().__init__(line)
        self.line = line
        self.parent = line.parent
        self.ioctl_count = 0

    def __str__(self):
        return 'file handle'

    def add_line(self, line):
        """Record a line for the handle"""
        if line.function == 'dfuse_cb_ioctl':
            if line.get_field(2) == 'Returning':
                self.ioctl_count += 1
            return
        print(line.get_msg())


class RootInodeHandle(DLogEntity):
    """Inode handle"""

    def __init__(self, line):
        super().__init__(line)
        self.line = line
        self.dentry = None
        self.parent = line.parent

    def __str__(self):
        if self.dentry is None:
            return 'Root inode, no dentry'
        return "Root inode: {} name '{}'".format(self.line.descriptor, self.dentry)

    def add_line(self, line):
        """Record a line for the handle"""
        if self.dentry is not None:
            return

        if line.get_field(2) == 'file':
            self.dentry = line.get_field(3)[1:-1]


class NullHandle(DLogEntity):
    """Handle that doesn't do anything"""

    def __init__(self, line, dname):
        self.dname = dname
        super().__init__(line)
        self.quiet = True
        self['type'] = dname

    def add_line(self, _line):
        """Record a line for the handle"""

    def ___str__(self):
        return ''

    def name(self):
        """Override the name function"""
        return self.dname


class RootHandle(DLogEntity):
    """Root of everything"""

    def __init__(self, line):
        super().__init__(line)
        self.line = line
        self.parent = None

    def add_line(self, _line):
        """Record a line for the handle"""

    def __str__(self):
        return 'root'


class ReaddirTracer():
    """Parse readdir logs"""

    def __init__(self):
        self.all_handles = {}
        self.root = None
        self._reports = []

    def report(self):
        """Print some data"""
        print('Readdir report is')
        print('\n'.join(self._reports))
        print('Root is:')
        print(self.root)
        pprint.PrettyPrinter(indent=2, compact=True).pprint(self.root)

    def add_line(self, line):
        """Parse a new line"""
        def new_desc():
            """Create a new object"""
            # pylint: disable=too-many-return-statements
            if line.get_field(2) == 'Registered':
                dtype = line.get_field(4)
                if not dtype.endswith("'"):
                    dtype += ' ' + line.get_field(5)
                dtype = dtype.strip("'")

                if dtype == 'inode':
                    return InodeHandle(line)
                if dtype == 'readdir':
                    return ReaddirHandle(line)
                if dtype == 'open handle':
                    if line.filename.endswith('opendir.c'):
                        return DirHandle(line)
                    return FileHandle(line)
                if dtype == 'root_inode':
                    return RootInodeHandle(line)
                if line.get_field(6) == 'root':
                    self.root = RootHandle(line)
                    return self.root
                return NullHandle(line, dtype)
            return NullHandle(line, 'unknown')

        if not line.trace and line.is_free:
            addr = line.get_field(5).rstrip('.')
            if addr not in self.all_handles:
                return
            handle = self.all_handles[addr]
            if not isinstance(handle, (NullHandle)):
                print(handle.parent)
                if handle.parent in self.all_handles:
                    parent = self.all_handles[handle.parent]
                    if not parent.quiet:
                        self._reports.append(str(parent))
                self._reports.append(str(handle))
                self._reports.append(handle.stats())
            handle.delete()
            del self.all_handles[addr]
            return

        if not line.trace:
            return

        if line.descriptor in self.all_handles:
            self.all_handles[line.descriptor].add_line(line)
            self.all_handles[line.descriptor].mark_line(line)
            return
        handle = new_desc()
        self.all_handles[line.descriptor] = handle
        if not isinstance(handle, RootHandle):
            self.all_handles[line.parent].add_child(handle)

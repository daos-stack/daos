#!/usr/bin/env python
'''
  (C) Copyright 2020 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function
import os
import copy
import sys

from storage_estimator.vos_structures import VosObject, AKey, DKey, Container, Containers, VosValue, Overhead, ValType, KeyType
from storage_estimator.util import CommonBase, ObjectClass


class FileInfo(object):
    def __init__(self, size):
        self.st_size = size


class Entry(object):
    def __init__(self, name, path):
        self.path = path
        self.name = name

    def stat(self, follow_symlinks):
        if follow_symlinks:
            file_size = os.stat(self.path).st_size
        else:
            file_size = os.lstat(self.path).st_size
        return FileInfo(file_size)


class AverageFS(CommonBase):
    def __init__(self, arg):
        super(AverageFS, self).__init__()

        if isinstance(arg, DFS):
            self._dfs = arg
        elif isinstance(arg, ObjectClass):
            self._dfs = DFS(arg)
        else:
            raise TypeError(
                'arg must be of type {0} or {1}'.format(
                    type(DFS), type(ObjectClass)))

        self._total_symlinks = 0
        self._avg_symlink_size = 0
        self._dir_name_size = 0
        self._total_dirs = 0
        self._avg_name_size = 0
        self._total_files = 0

    def set_verbose(self, verbose):
        self._dfs.set_verbose(verbose)
        self._verbose = verbose

    def set_dfs_inode(self, akey):
        self._dfs.set_dfs_inode(akey)

    def set_io_size(self, io_size):
        self._dfs.set_io_size(io_size)

    def set_chunk_size(self, chunk_size):
        self._dfs.set_chunk_size(chunk_size)

    def set_dfs_file_meta(self, dkey):
        self._dfs.set_dfs_file_meta(dkey)

    def set_total_symlinks(self, links):
        self._check_value_type(links, int)
        self._total_symlinks = links

    def set_avg_symlink_size(self, links_size):
        self._check_value_type(links_size, int)
        self._avg_symlink_size = links_size

    def set_total_directories(self, dirs):
        self._check_value_type(dirs, int)
        self._total_dirs = dirs

    def set_avg_dir_name_size(self, name_size):
        self._check_value_type(name_size, int)
        self._dir_name_size = name_size

    def set_avg_name_size(self, name_size):
        self._check_value_type(name_size, int)
        self._avg_name_size = name_size
        self._debug(
            'using {0} average file name size'.format(self._avg_name_size))

    def get_dfs(self):
        new_dfs = self._dfs.copy()
        new_dfs = self._calculate_average_dir(new_dfs)
        self._debug('Gloabal Stripe Stats')
        new_dfs._all_ec_stats.show()

        return new_dfs

    def _calculate_average_dir(self, dfs):
        if self._total_dirs > 0:
            oid = dfs.create_dir_obj(self._total_dirs)
            self._debug('Populating directories')
            avg_name = 'x' * self._avg_name_size

            # add symlinks
            symlink_per_dir = self._total_symlinks // self._total_dirs
            symlink_per_dir += (self._total_symlinks % self._total_dirs) > 0
            self._debug(
                'adding {0} symlinks of name size {1} bytes and size {2} bytes per directory'.format(
                    symlink_per_dir,
                    self._avg_name_size,
                    self._avg_symlink_size))
            dfs.add_symlink(
                oid,
                avg_name,
                self._avg_symlink_size,
                symlink_per_dir)

            # add dirs
            avg_dir_name = 'x' * self._dir_name_size
            self._debug(
                'adding 1 directory of name size {0} bytes per directory'.format(
                    self._dir_name_size))
            dfs.add_dummy(oid, avg_dir_name)

            # add files
            files_per_dir = self._total_files // self._total_dirs
            files_per_dir += (self._total_files % self._total_dirs) > 0
            self._debug(
                'adding {0} files of name size {1} per directory'.format(
                    files_per_dir, self._avg_name_size))
            dfs.add_dummy(oid, avg_name, files_per_dir)

        return dfs

    def add_average_file(self, count_files, file_size):
        self._dfs.create_file_obj(file_size, count_files)
        self._total_files += count_files


class CellStats(CommonBase):
    def __init__(self, verbose=False):
        super(CellStats, self).__init__()
        self.parity = 0
        self.payload = 0
        self.set_verbose(verbose)

    def add(self, stats):
        self.parity += stats.parity
        self.payload += stats.payload

    def mul(self, mul):
        self.parity *= mul
        self.payload *= mul

    def show(self):
        self._debug('Number of data cells:   {0}'.format(self.payload))
        self._debug('Number of parity cells: {0}'.format(self.parity))


class DFS(CommonBase):
    def __init__(self, oclass):
        super(DFS, self).__init__()
        self._objects = []
        self._chunk_size = 1048576
        self._io_size = 131072
        self._all_ec_stats = CellStats()
        self._oclass = oclass

        self._dkey0 = self._create_default_dkey0()
        self._dfs_inode_akey = self._create_default_inode_akey()

    def set_verbose(self, verbose):
        self._verbose = verbose
        self._all_ec_stats.set_verbose(verbose)

    def set_io_size(self, io_size):
        self._io_size = io_size

    def set_chunk_size(self, chunk_size):
        self._chunk_size = chunk_size

    def set_dfs_file_meta(self, dkey):
        self._check_value_type(dkey, DKey)
        self._dkey0 = dkey

    def set_dfs_inode(self, akey):
        self._check_value_type(akey, AKey)
        self._dfs_inode_akey = akey

    def get_container(self):
        container = Container(objects=self._objects)

        return container

    def copy(self):
        new_dfs = DFS(self._oclass)
        new_dfs._io_size = copy.deepcopy(self._io_size)
        new_dfs._chunk_size = copy.deepcopy(self._chunk_size)
        new_dfs._dkey0 = copy.deepcopy(self._dkey0)
        new_dfs._dfs_inode_akey = copy.deepcopy(self._dfs_inode_akey)
        new_dfs._objects = copy.deepcopy(self._objects)
        new_dfs._verbose = copy.deepcopy(self._verbose)
        new_dfs._all_ec_stats = copy.deepcopy(self._all_ec_stats)

        return new_dfs

    def reset(self):
        self._objects = []

    def add_obj(self):
        oid = len(self._objects)
        self._objects.append(VosObject())

        return oid

    def remove_obj(self, oid):
        self._objects.pop(oid)

    def add_symlink(self, oid, name, link_size, dkey_count=1):
        akey = copy.deepcopy(self._dfs_inode_akey)
        value = VosValue(size=link_size)
        akey.add_value(value)

        dkey = DKey(key=name)
        dkey.set_count(dkey_count)
        dkey.add_value(akey)

        self._objects[oid].add_value(dkey)

    def _add_entry(self, oid, name, dkey_count=1):
        dkey = DKey(key=name)
        dkey.set_count(dkey_count)
        dkey.add_value(self._dfs_inode_akey)

        self._objects[oid].add_value(dkey)

    def add_dummy(self, oid, name, dkey_count=1):
        self._add_entry(oid, name, dkey_count)

    def add_dir(self, oid, name, dkey_count=1):
        self._add_entry(oid, name, dkey_count)

    def add_file(self, oid, name, file_size, dkey_count=1):
        self._add_entry(oid, name, dkey_count)
        self.create_file_obj(file_size, dkey_count)

    def update_object_count(self, oid, count):
        self._objects[oid].set_count(count)

    def show_stats(self):
        self._all_ec_stats.show()

    def _create_default_dkey0(self):
        akey = AKey(
            key_type=KeyType.INTEGER,
            overhead=Overhead.META,
            value_type=ValType.SINGLE)
        value = VosValue(count=3, size=64)
        akey.add_value(value)
        dkey = DKey(
            key_type=KeyType.INTEGER,
            overhead=Overhead.META,
            akeys=[akey])

        return dkey

    def _create_default_inode_akey(self, key='DFS_INODE', size=64):
        value = VosValue(size=size)
        akey = AKey(key=key,
                    overhead=Overhead.META,
                    value_type=ValType.ARRAY)
        akey.add_value(value)
        return akey

    def _create_file_akey(self, size):
        self._check_positive_number(size)
        count = size // self._io_size
        remainder = size % self._io_size

        akey = AKey(
            key_type=KeyType.INTEGER,
            overhead=Overhead.USER,
            value_type=ValType.ARRAY)

        if count > 0:
            value = VosValue(count=count, size=self._io_size)
            akey.add_value(value)

        if remainder > 0:
            value = VosValue(size=remainder)
            akey.add_value(value)

        return akey

    def _create_file_dkey(self, size):
        akey = self._create_file_akey(size)
        dkey = DKey(
            key_type=KeyType.INTEGER,
            overhead=Overhead.USER,
            akeys=[akey])

        return dkey

    def _add_chunk_size_elements(self, file_object, file_size, parity_stats):
        count = file_size // self._chunk_size

        self._debug(
            'adding {0} chunk(s) of size {1}'.format(
                count, self._chunk_size))

        if count > 0:
            self._add_parity_cells(
                file_object,
                count,
                self._chunk_size,
                parity_stats)

            replicas = self._oclass.get_file_replicas()
            count *= replicas

            dkey = self._create_file_dkey(self._chunk_size)
            dkey.set_count(count)
            parity_stats.payload += count
            file_object.add_value(dkey)

    def _add_chunk_size_remainder(self, file_object, file_size, parity_stats):
        count = self._oclass.get_file_replicas()
        remainder = file_size % self._chunk_size

        self._debug('adding {0} chunk(s) of size {1}'.format(count, remainder))

        if remainder > 0:

            dkey = self._create_file_dkey(remainder)
            dkey.set_count(count)
            file_object.add_value(dkey)
            parity_stats.payload += count
            self._add_parity_cells(file_object, count, remainder, parity_stats)

    def _add_parity_cells(self, file_object, count, cell_size, parity_stats):
        parity = self._oclass.get_file_parity()

        if parity == 0:
            return

        stripe = self._oclass.get_file_stripe()

        if count % stripe:
            parity_cells = (count // stripe) * parity + parity
        else:
            parity_cells = (count // stripe) * parity

        dkey = self._create_file_dkey(cell_size)
        dkey.set_count(parity_cells)
        file_object.add_value(dkey)
        parity_stats.parity += parity_cells

    def create_dir_obj(self, identical_dirs=1):
        parity_stats = CellStats(self._verbose)
        oid = self.add_obj()

        self._debug('adding {0} directory(s)'.format(identical_dirs))

        count = identical_dirs
        count *= self._oclass.get_dir_replicas()
        parity_stats.payload = count
        parity_cells = self._oclass.get_dir_parity()
        count += parity_cells

        self._objects[oid].set_count(count)
        self._objects[oid].set_num_of_targets(self._oclass.get_dir_targets())

        parity_stats.parity += parity_cells * identical_dirs

        parity_stats.show()
        self._all_ec_stats.add(parity_stats)

        return oid

    def create_file_obj(self, file_size, identical_files=1):
        parity_stats = CellStats(self._verbose)

        self._debug(
            'adding {0} file(s) of size of {1}'.format(
                identical_files, file_size))
        file_object = VosObject()
        file_object.set_num_of_targets(self._oclass.get_file_targets())
        file_object.set_count(identical_files)

        self._add_file_dkey0(file_object, parity_stats)
        self._add_chunk_size_elements(file_object, file_size, parity_stats)
        self._add_chunk_size_remainder(file_object, file_size, parity_stats)

        parity_stats.mul(identical_files)
        parity_stats.show()
        self._all_ec_stats.add(parity_stats)

        self._objects.append(file_object)

    def _add_file_dkey0(self, file_object, parity_stats):
        replicas = self._oclass.get_file_replicas()
        parity = self._oclass.get_file_parity()
        count = replicas + parity
        parity_stats.payload += replicas
        parity_stats.parity += parity
        dkey = copy.deepcopy(self._dkey0)
        dkey.set_count(count)
        file_object.add_value(dkey)


class FileSystemExplorer(CommonBase):
    def __init__(self, path, oclass):
        super(FileSystemExplorer, self).__init__()
        self._path = path
        self._queue = []
        self._count_files = 0
        self._count_dir = 0
        self._count_sym = 0
        self._count_error = 0
        self._file_size = 0
        self._sym_size = 0
        self._name_size = 0

        self._oid = 0
        self._dfs = DFS(oclass)

    def set_dfs_inode(self, akey):
        self._dfs.set_dfs_inode(akey)

    def set_io_size(self, io_size):
        self._dfs.set_io_size(io_size)

    def set_chunk_size(self, chunk_size):
        self._dfs.set_chunk_size(chunk_size)

    # TODO: Get the D-Key 0 information from the DAOS Array Object
    def set_dfs_file_meta(self, dkey):
        self.dfs.set_dfs_file_meta(dkey)

    def explore(self):
        self._debug('processing path: {0}'.format(self._path))
        self._dfs.set_verbose(self._verbose)
        self._traverse_directories()

    def print_stats(self):
        pretty_size = self._to_human(self._file_size)
        pretty_file_size = self._to_human(self._file_size)
        pretty_sym_size = self._to_human(self._sym_size)
        pretty_name_size = self._to_human(self._name_size)
        total_size = self._file_size + self._sym_size + self._name_size
        pretty_total_size = self._to_human(total_size)
        total_count = self._count_files + self._count_dir
        total_count += self._count_sym + self._count_error

        self._info('')
        self._info('Summary:')
        self._info('')
        self._info(
            '  directories {0} count {1}'.format(
                self._count_files,
                pretty_name_size))
        self._info(
            '  files       {0} count {1}'.format(
                self._count_dir,
                pretty_file_size))
        self._info(
            '  symlinks    {0} count {1}'.format(
                self._count_sym,
                pretty_sym_size))
        self._info('  errors      {0} count'.format(self._count_error))
        self._info('')
        self._info('  total count   {0}'.format(total_count))

        self._info(
            '  total size    {0} ({1} bytes)'.format(
                pretty_total_size,
                total_size))

        self._info('')

    def get_dfs(self):
        self._debug('Gloabal Stripe Stats')
        self._dfs._all_ec_stats.show()

        container = self._dfs.get_container()

        return self._dfs

    def _get_avg_file_name_size(self):
        total_items = self._count_files + self._count_dir + self._count_sym
        if total_items == 0:
            avg_file_name_size = 0
        else:
            avg_file_name_size = self._name_size // total_items

        self._debug(
            '  assuming average file name size of {0} bytes'.format(avg_file_name_size))
        return avg_file_name_size

    def get_dfs_average(self):
        averageFS = AverageFS(self._dfs)
        averageFS.set_verbose(self._verbose)
        averageFS.set_total_symlinks(self._count_sym)
        averageFS.set_avg_symlink_size(self._sym_size)
        averageFS.set_total_directories(self._count_dir)

        averageFS.set_avg_name_size(self._get_avg_file_name_size())

        if self._count_files > 0:
            avg_file_size = self._file_size // self._count_files
            self._debug(
                '  assuming average file size of {0} bytes'.format(avg_file_size))
            averageFS.add_average_file(self._count_files, avg_file_size)

        dfs = averageFS.get_dfs()

        container = dfs.get_container()

        return dfs

    def _process_stats(self, container):
        stats = {
            "objects": 0,
            "dkeys": 0,
            "akeys": 0,
            "values": 0,
            "dkey_size": 0,
            "akey_size": 0,
            "value_size": 0}

        for object in container["objects"]:
            obj_count = object.get("count", 11)
            if obj_count == 0:
                continue

            stats["objects"] += obj_count

            for dkey in object["dkeys"]:
                dkey_count = dkey.get("count", 1)
                if dkey_count == 0:
                    continue

                total_dkeys = obj_count * dkey_count
                stats["dkey_size"] += dkey.get("size", 0) * total_dkeys
                stats["dkeys"] += obj_count * dkey_count

                for akey in dkey["akeys"]:
                    akey_count = akey.get("count", 1)
                    if akey_count == 0:
                        continue

                    total_akeys = obj_count * dkey_count * akey_count
                    stats["akey_size"] += akey.get("size", 0) * total_akeys
                    stats["akeys"] += total_akeys

                    for value in akey["values"]:
                        value_count = value.get("count", 1)
                        if value_count == 0:
                            continue

                        total_values = obj_count * dkey_count * akey_count * value_count
                        stats["value_size"] += value.get("size",
                                                         0) * total_values
                        stats["values"] += total_akeys

        return stats

    def _read_directory_3(self, file_path):
        with os.scandir(file_path) as it:
            count = 0

            for entry in it:
                self._name_size += len(entry.name.encode("utf-8"))
                if entry.is_symlink():
                    self._process_symlink(entry)
                    count += 1
                elif entry.is_dir():
                    self._process_dir(entry)
                    count += 1
                elif entry.is_file():
                    self._process_file(entry)
                    count += 1
                else:
                    self._error(
                        'found unknown object (skipped): {0}'.format(
                            entry.name))
            if count == 0:
                self._process_empty_dir()

    def _read_directory_2(self, file_path):
        items = os.listdir(file_path)

        if not items:
            self._process_empty_dir()

        for item in items:
            target = os.path.join(file_path, item)

            entry = Entry(item, target)
            self._name_size += len(entry.name.encode("utf-8"))

            if os.path.islink(target):
                self._process_symlink(entry)
            elif os.path.isdir(target):
                self._process_dir(entry)
            elif os.path.isfile(target):
                self._process_file(entry)
            else:
                print(
                    'Error: found unknown object (skipped): {0}'.format(
                        entry.name))

    def _read_directory(self, file_path):
        try:
            if sys.version_info < (3, 5):
                self._read_directory_2(file_path)
            else:
                self._read_directory_3(file_path)

        except OSError:
            self._error('permission denied (skipped): {0}'.format(file_path))
            self._process_error(file_path)
        except Exception as err:
            self._error('opening dir {0}'.format(err))
            self._process_error(file_path)

    def _process_empty_dir(self):
        self._dfs.remove_obj(self._oid)

    def _process_error(self, file_path):
        self._debug(
            'a adding dummy entry {0} for {1}'.format(
                self._oid, file_path))
        self._dfs.add_dummy(self._oid, 'unknown')
        self._count_error += 1

    def _process_symlink(self, entry):
        self._debug('symlink:   {0}'.format(entry.name))
        info = entry.stat(follow_symlinks=False)
        self._dfs.add_symlink(self._oid, entry.name, info.st_size)
        self._sym_size += info.st_size
        self._count_sym += 1

    def _process_dir(self, entry):
        self._debug('directory: {0}'.format(entry.name))
        self._dfs.add_dir(self._oid, entry.name)
        self._enqueue_path(entry.path)
        self._count_dir += 1

    def _process_file(self, entry):
        self._debug('file:      {0}'.format(entry.name))
        info = entry.stat(follow_symlinks=False)
        self._dfs.add_file(self._oid, entry.name, info.st_size)
        self._file_size += info.st_size
        self._count_files += 1

    def _enqueue_path(self, path):
        path = os.path.realpath(path)
        self._queue.append(path)

    def _traverse_directories(self):
        self._reset_stats()
        self._dfs.reset()
        self._enqueue_path(self._path)

        while(self._queue):
            file_path = self._queue.pop(0)
            self._oid = self._dfs.create_dir_obj()
            self._debug('entering {0}'.format(file_path))
            self._read_directory(file_path)

    def _reset_stats(self):
        self._oid = 0
        self._count_files = 0
        self._count_dir = 0
        self._count_sym = 0
        self._count_error = 0
        self._file_size = 0
        self._sym_size = 0
        self._name_size = 0

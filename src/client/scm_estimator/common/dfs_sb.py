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
import json
import sys
import os
import ctypes

from pydaos.raw import daos_cref
from scm_estimator.vos_structures import ValType, Overhead, AKey, VosValue, DKey, VosObject

header = '''---
# Sample conflig file DFS files and directories
num_shards: 1000
'''

remainder = '''
# Assumes 16 bytes for file name
dirent_key: &dirent
  count: 1000000
  size: 16
  akeys: [*dfs_inode]

dir_obj: &dir
  dkeys: [*dirent]

superblock: &sb
  dkeys: [*dfs_sb_metadata]

array_akey: &file_data
  count: 1
  type: hashed
  size: 1
  overhead: meta
  value_type: array
  values: [{'count': 1, 'size': 4096, 'aligned': 'Yes'}]

array_meta: &file_meta
  count: 1
  type: integer
  overhead: meta
  value_type: single_value
  values: [{'count': 3, 'size': 64, 'aligned': 'Yes'}]

file_dkey_key0: &file_dkey0
  count: 1
  type: integer
  overhead: user
  akeys: [*file_data, *file_meta]

file_dkey_key: &file_dkey
  count: 1
  type: integer
  overhead: user
  akeys: [*file_data]

file_key: &file
  count: 1000000
  dkeys: [*file_dkey0, *file_dkey]

posix_key: &posix
  objects: [*sb, *file, *dir]

containers: [*posix]
'''


def _build_values(count=1, size=1, aligned='Yes'):
    values = {}
    values['count'] = count
    values['size'] = size
    values['aligned'] = aligned

    return values


def _print_akey(iod, overhead='meta'):
    iov_buf = iod.iod_name.iov_buf
    iov_buf_len = iod.iod_name.iov_buf_len
    akey_str = ctypes.string_at(iov_buf, iov_buf_len)

    if iod.iod_type == 1:
        iod_type = 'single_value'

    if iod.iod_type == 2:
        iod_type = 'array'

    values = _build_values(int(iod.iod_nr), int(iod.iod_size))

    key = akey_str.lower().decode('utf-8')

    akey = '''
{0}: &{1}
  count: 1
  type: hashed
  size: {2}
  overhead: {3}
  value_type: {4}
  values: [{5}]
'''.format(key, key, iov_buf_len, overhead, iod_type, values)

    return key, akey


def _list_2_str(values):
    values_str = ''
    for value in values:
        if values_str:
            values_str += ', '
        values_str += '*{0}'.format(value)

    values_str = '[ ' + values_str + ' ]'

    return values_str


def _print_dkey(dkey, akeys):
    dkey_str = ctypes.string_at(dkey.iov_buf, dkey.iov_buf_len)
    key = dkey_str.lower().decode('utf-8')

    buf = '''
{0}: &dfs_sb_metadata
  count: 1
  type: hashed
  size: {1}
  overhead: meta
  akeys: {2}
'''.format(key, dkey.iov_len, _list_2_str(akeys))

    return buf


def _print_dfs_inode(key, size):
    values = _build_values(size=size)

    buf = '''
dfs_inode: &dfs_inode
  count: 1
  type: hashed
  size: {0}
  overhead: meta
  value_type: array
  values: [{1}]
'''.format(key, values)

    return buf


def _print_dfs(dkey, iods, akey_count, dfs_entry_key_size, dfs_entry_size):
    akeys = []
    buf = ''

    for i in range(0, akey_count.value):
        key, akey_str = _print_akey(iods[i])
        akeys.append(key)
        buf += akey_str

    buf += _print_dkey(dkey, akeys)
    buf += _print_dfs_inode(dfs_entry_key_size.value, dfs_entry_size.value)

    return buf


def _create_akey(iod):
    iov_buf = iod.iod_name.iov_buf
    iov_buf_len = iod.iod_name.iov_buf_len
    key = ctypes.string_at(iov_buf, iov_buf_len)

    if iod.iod_type == 1:
        iod_type = ValType.SINGLE

    if iod.iod_type == 2:
        iod_type = ValType.ARRAY

    overhead = Overhead.META
    akey = AKey(
        key=key.decode('utf-8'),
        value_type=iod_type,
        overhead=overhead)
    value = VosValue(
        count=int(
            iod.iod_nr), size=int(
            iod.iod_size))
    akey.add_value(value)

    return akey


def _parse_dfs_sb_dkey(dkey_raw, iods, akey_count):
    key = ctypes.string_at(dkey_raw.iov_buf, dkey_raw.iov_buf_len)
    overhead = Overhead.META
    dkey = DKey(key=key.decode('utf-8'), overhead=overhead)

    for i in range(0, akey_count.value):
        akey = _create_akey(iods[i])
        dkey.add_value(akey)

    return dkey


def _parse_dfs_akey_inode(dfs_entry_key_size, dfs_entry_size):
    key = 'x' * dfs_entry_key_size
    overhead = Overhead.META
    value_type = ValType.ARRAY
    akey = AKey(
        key=key,
        overhead=overhead,
        value_type=value_type)
    value = VosValue(size=dfs_entry_size)
    akey.add_value(value)

    return akey


class BASE_CLASS(object):
    def __init__(self, lib_name):
        self._lib = self._load_lib(lib_name)

    def _load_lib(self, lib_name):
        current_path = os.path.dirname(os.path.abspath(__file__))
        try:
            lib_path = os.path.join(current_path, '../../..')

            libdfs = ctypes.CDLL(
                os.path.join(lib_path, lib_name),
                mode=ctypes.DEFAULT_MODE)

        except OSError as err:
            raise Exception(
                'failed to load {0} library: {1}'.format(
                    lib_name, err))

        return libdfs


class VOS_SIZE(BASE_CLASS):
    def __init__(self):
        super(VOS_SIZE, self).__init__('libvos_size.so')
        self._data_ptr = ctypes.c_char_p(0)

    def __del__(self):
        self._lib.free_string(self._data_ptr)

    def get_vos_size_str(self, alloc_overhead):
        print('  Reading VOS structures from current installation')
        data_ptr = self._lib.get_vos_structure_sizes_yaml(
            ctypes.c_int(alloc_overhead))

        if self._data_ptr == 0:
            raise Exception(
                'failed to retrieve the VOS structure sizes: {0}'.format(data))

        data_cstr = ctypes.c_char_p(data_ptr)
        self._data_ptr = data_ptr

        return data_cstr.value.decode('utf-8')


class DFS_SB(BASE_CLASS):
    def __init__(self):
        super(DFS_SB, self).__init__('libdfs_internal.so')
        self._dkey = daos_cref.IOV()
        self._iods = ctypes.pointer(daos_cref.DaosIODescriptor())
        self._akey_count = ctypes.c_int()
        self._dfs_entry_key_size = ctypes.c_int()
        self._dfs_entry_size = ctypes.c_int()
        self._ready = False

    def __del__(self):
        self._lib.dfs_free_sb_layout(ctypes.byref(self._iods))

    def _dfs_get_sb_layout(self):
        ret = self._lib.dfs_get_sb_layout(
            ctypes.byref(self._dkey),
            ctypes.byref(self._iods),
            ctypes.byref(self._akey_count),
            ctypes.byref(self._dfs_entry_key_size),
            ctypes.byref(self._dfs_entry_size))
        self._ready = True

        if ret != 0:
            raise Exception(
                'failed to retrieve the DFS Super Block. RC: {0}'.format(ret))

    def get_dfs_str(self):
        if not self._ready:
            self._dfs_get_sb_layout()
        return _print_dfs(
            self._dkey,
            self._iods,
            self._akey_count,
            self._dfs_entry_key_size,
            self._dfs_entry_size)

    def get_dfs_sb_dkey(self):
        if not self._ready:
            self._dfs_get_sb_layout()
        return _parse_dfs_sb_dkey(self._dkey, self._iods, self._akey_count)

    def get_dfs_inode_akey(self):
        if not self._ready:
            self._dfs_get_sb_layout()
        return _parse_dfs_akey_inode(
            self._dfs_entry_key_size.value,
            self._dfs_entry_size.value)


def print_daos_version():
    try:
        current_path = os.path.dirname(os.path.abspath(__file__))
        with open(os.path.join(current_path, '../../../daos/VERSION'),
                  'r') as version_file:
            daos_version = version_file.read().rstrip()
    except OSError:
        daos_version = '0.0.0'

    print('Using DAOS version: {0}'.format(daos_version))


def get_dfs_sb_obj():
    try:
        dfs_sb = DFS_SB()
        dkey = dfs_sb.get_dfs_sb_dkey()
        dfs_inode = dfs_sb.get_dfs_inode_akey()
    except Exception as err:
        raise Exception(
            'Failed to get the DFS superblock VOS object: {0}'.format(err))

    sb_obj = VosObject()
    sb_obj.add_value(dkey)

    root_dkey = DKey(key='/', overhead=Overhead.USER)
    root_dkey.add_value(dfs_inode)
    sb_obj.add_value(root_dkey)

    return sb_obj


def get_dfs_inode_akey():
    try:
        dfs_sb = DFS_SB()
        akey = dfs_sb.get_dfs_inode_akey()
    except Exception as err:
        raise Exception('failed to retrieve to DFS inode: {0}'.format(err))

    return akey


def get_dfs_sb():
    try:
        dfs_sb = DFS_SB()
        buf = dfs_sb.get_dfs_str()
    except Exception as err:
        raise Exception('failed to retrieve DFS Superblock: {0}'.format(err))

    return buf


def get_dfs_example():
    buf = header
    buf += get_dfs_sb()
    buf += remainder

    return buf

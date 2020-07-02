#!/usr/bin/env python
'''
  (C) Copyright 2019 Intel Corporation.

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

Usage: vos_size_dfs_sample.py -f output_file.yaml
'''
from __future__ import print_function
import json
import sys
import os
import ctypes

from pydaos.raw import daos_cref


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
  size: 1
  overhead: meta
  value_type: array
  values: [{'count': 1, 'size': 4096}]

array_meta: &file_meta
  size: 19
  overhead: meta
  value_type: single_value
  values: [{'size': 24}]

file_dkey_key0: &file_dkey0
  count: 1
  type: integer
  akeys: [*file_data, *file_meta]

file_dkey_key: &file_dkey
  count: 1
  type: integer
  akeys: [*file_data]

file_key: &file
  count: 1000000
  dkeys: [*file_dkey0, *file_dkey]

posix_key: &posix
  objects: [*sb, *file, *dir]

containers: [*posix]
'''


def print_akey(f, iod, overhead='meta'):
    iov_buf = iod.iod_name.iov_buf
    iov_buf_len = iod.iod_name.iov_buf_len
    akey_str = ctypes.string_at(iov_buf, iov_buf_len)

    if iod.iod_type == 1:
        iod_type = 'single_value'

    if iod.iod_type == 2:
        iod_type = 'array'

    values = {}
    values['count'] = int(iod.iod_nr)
    values['size'] = int(iod.iod_size)

    key = akey_str.lower().decode('utf-8')

    f.write('{0}: &{1}\n'.format(key, key))
    f.write('  size: {0}\n'.format(iov_buf_len))
    f.write('  overhead: {0}\n'.format(overhead))
    f.write('  value_type: {0}\n'.format(iod_type))
    f.write('  values: [{0}]\n'.format(values))
    f.write('\n')

    return key


def list_2_str(values):
    values_str = ""
    for value in values:
        if values_str:
            values_str += ", "
        values_str += "*{0}".format(value)

    values_str = "[ " + values_str + " ]"

    return values_str


def print_dkey(f, dkey, akeys):
    dkey_str = ctypes.string_at(dkey.iov_buf, dkey.iov_buf_len)
    key = dkey_str.lower().decode('utf-8')

    f.write('{0}: &dfs_sb_metadata\n'.format(key))
    f.write('  size: {0}\n'.format(dkey.iov_len))
    f.write('  overhead: meta\n')
    akeys_str = list_2_str(akeys)
    f.write('  akeys: {}\n'.format(list_2_str(akeys)))
    f.write('\n')


def print_dfs_inode(f, size):
    f.write('dfs_inode: &dfs_inode\n')
    f.write('  type: integer\n')
    f.write('  overhead: meta\n')
    f.write('  value_type: array\n')
    values = {'count': 1, 'size': size}
    f.write('  values: [{0}]\n'.format(values))
    f.write('\n')


def print_dfs(f, dkey, iods, akey_count, dfs_entry_size):
    akeys = []

    for i in range(0, akey_count.value):
        akeys.append(print_akey(f, iods[i]))

    print_dkey(f, dkey, akeys)
    print_dfs_inode(f, dfs_entry_size.value)


def process_dfs(libdfs, f):
    dkey = daos_cref.IOV()
    iods = ctypes.pointer(daos_cref.DaosIODescriptor())
    akey_count = ctypes.c_int()
    dfs_entry_size = ctypes.c_int()

    ret = libdfs.dfs_get_sb_layout(
        ctypes.byref(dkey),
        ctypes.byref(iods),
        ctypes.byref(akey_count),
        ctypes.byref(dfs_entry_size))

    if ret != 0:
        raise Exception(
            'failed to retrieve the DFS Super Block. RC: {0}'.format(ret))

    print_dfs(f, dkey, iods, akey_count, dfs_entry_size)
    libdfs.dfs_free_sb_layout(ctypes.byref(iods))

def load_libdfs():
    current_path = os.path.dirname(os.path.abspath(__file__))
    try:
        build_vars = os.path.join(current_path, '../lib/daos/.build_vars.json')
        with open(build_vars, 'r') as f:
            data = json.load(f)
            libdfs_path = os.path.join(data['PREFIX'], 'lib64')

        libdfs = ctypes.CDLL(
            os.path.join(libdfs_path, 'libdfs_internal.so'),
            mode=ctypes.DEFAULT_MODE)

    except OSError as err:
        raise Exception("failed to load libdfs_internal library: {0}".format(err))

    return libdfs


def print_daos_version():
    try:
        current_path = os.path.dirname(os.path.abspath(__file__))
        with open(os.path.join(current_path, '../lib64/daos/VERSION'),
                  "r") as version_file:
            daos_version = version_file.read().rstrip()
    except OSError:
        daos_version = '0.0.0'

    print("Using DAOS version: {0}".format(daos_version))


def print_destination_file(file_name):
    file_name = os.path.normpath(file_name)
    print('Output file: {0}'.format(file_name))


def create_file(file_name):
    try:

        libdfs = load_libdfs()

        if not file_name.endswith('.yaml'):
            file_name = file_name + '.yaml'

        print_destination_file(file_name)
        with open(file_name, 'w') as f:
            f.write(header)
            process_dfs(libdfs, f)
            f.write(remainder)

    except Exception as err:
        print('\nERROR: {0}'.format(err))
        sys.exit(-1)


def main():
    import argparse

    parser = argparse.ArgumentParser(
        description='Create a VOS file example of the DFS layout')

    parser.add_argument(
        '-f',
        '--file_name',
        type=str,
        default='vos_dfs_sample.yaml',
        help='Output file name')

    args = parser.parse_args()

    print_daos_version()

    create_file(args.file_name)


if __name__ == '__main__':
    main()

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

  Takes customer csv data and parses it.  Mostly checking in for backup purpose
'''
from __future__ import print_function
import sys

from storage_estimator.explorer import AverageFS
from storage_estimator.dfs_sb import get_dfs_inode_akey
from storage_estimator.util import ProcessBase

FILE_SIZES = ['4k', '64k', '128k', '256k', '512k', '768k', '1m', '8m', '64m',
              '128m', '1g', '10g', '100g', '250g', '500g', '1t', '10t', '100t']


class ProcessCSV(ProcessBase):
    def __init__(self, args):
        super(ProcessCSV, self).__init__(args)

    def run(self):
        fse = self._ingest_csv()
        config_yaml = self._get_yaml_from_dfs(fse)

        yaml_str = self._dump_yaml(config_yaml)
        self._create_file(self._args.output, yaml_str)
        self._process_yaml(config_yaml)

    def _ingest_csv(self):
        """Parse csv and produce yaml input to vos_size.py"""

        value_dict = {}
        with open(self._args.csv[0], "r") as csv_file:
            idx = 0
            fields = csv_file.readline().strip().split(',')
            values = csv_file.readline().strip().split(',')
            if len(fields) != len(values):
                raise Exception(
                    "CSV must provide one row of values that matches fields"
                    "Number of fields is {0}"
                    "Number of values is {1}".format(
                        len(fields), len(values)))
            for name in fields:
                value_dict[name] = values[idx]
                idx += 1

            count_dir = int(value_dict.get("dir_count", 0))
            dir_size = int(value_dict.get("dir_size", 0))
            count_files = int(value_dict.get("data_count", 0))
            count_symlink = int(value_dict.get("link_count", 0))

            symlink_size = int(value_dict.get("link_size", 0)
                               ) // int(value_dict.get("link_count", 1))

            total_items = count_files + count_symlink + count_dir
            unknown_items = int(
                value_dict.get(
                    "total_objects",
                    0)) - total_items

            self._debug("total files {0}".format(count_files))
            self._debug("total directories {0}".format(count_dir))
            self._debug("total symlinks {0}".format(count_symlink))
            self._debug("skipping {0} unsupported items".format(unknown_items))

            if count_dir > 0:
                items_per_dir = total_items // count_dir
            else:
                items_per_dir = 0

            self._debug(
                'assuming {0} items per directory'.format(items_per_dir))
            self._debug(
                'assuming average symlink size of {0} bytes'.format(symlink_size))

            afs = AverageFS()
            afs.set_verbose(self._verbose)
            inode_akey = get_dfs_inode_akey()
            afs.set_dfs_inode(inode_akey)
            afs.set_io_size(self._io_size)
            afs.set_chunk_size(self._chunk_size)
            afs.set_total_symlinks(count_symlink)
            afs.set_avg_symlink_size(symlink_size)
            afs.set_total_directories(count_dir)
            afs.set_avg_name_size(self._args.file_name_size)

            for size in FILE_SIZES:
                num_files = int(value_dict["%s_count" % size])
                total_size = int(value_dict["%s_size" % size])
                if num_files != 0:
                    avg_file_size = (total_size // num_files)
                    pretty_size = self._to_human(avg_file_size)
                    self._debug(
                        'found {0} files of {1} average size'.format(
                            num_files, pretty_size))
                    afs.add_average_file(num_files, avg_file_size)

            return afs

#!/usr/bin/env python
'''
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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

            # assumes that there is at least one directory
            count_dir = int(value_dict.get("dir_count", 1))
            total_dir_size = int(value_dict.get("dir_size", 32))
            count_files = int(value_dict.get("data_count", 0))
            count_symlink = int(value_dict.get("link_count", 0))
            total_symlink_size = int(value_dict.get("link_size", 0))

            if count_symlink > 0:
                symlink_size = total_symlink_size // count_symlink
            else:
                symlink_size = 0

            total_items = count_files + count_symlink + count_dir
            unknown_items = int(
                value_dict.get(
                    "total_objects",
                    0)) - total_items

            self._debug("total files {0}".format(count_files))
            self._debug("total directories {0}".format(count_dir))
            self._debug("total symlinks {0}".format(count_symlink))
            self._debug("skipping {0} unsupported items".format(unknown_items))

            items_per_dir = total_items // count_dir
            dir_name_size = total_dir_size // count_dir

            self._debug(
                'assuming {0} items per directory'.format(items_per_dir))
            self._debug(
                'assuming average symlink size of {0} bytes'.format(symlink_size))
            self._debug(
                'assuming average dir size of {0} bytes'.format(dir_name_size))

            afs = AverageFS(self._oclass)
            afs.set_verbose(self._verbose)
            inode_akey = get_dfs_inode_akey()
            afs.set_dfs_inode(inode_akey)
            afs.set_io_size(self._io_size)
            afs.set_chunk_size(self._chunk_size)
            afs.set_total_symlinks(count_symlink)
            afs.set_avg_symlink_size(symlink_size)
            afs.set_total_directories(count_dir)
            afs.set_avg_dir_name_size(dir_name_size)
            afs.set_avg_name_size(self._args.file_name_size)

            for size in FILE_SIZES:
                num_files = int(value_dict.get("%s_count" % size, 0))
                total_size = int(value_dict.get("%s_size" % size, 0))

                if num_files != 0:
                    avg_file_size = (total_size // num_files)
                    pretty_size = self._to_human(avg_file_size)
                    self._debug(
                        'found {0} files of {1} average size'.format(
                            num_files, pretty_size))
                    afs.add_average_file(num_files, avg_file_size)

            return afs

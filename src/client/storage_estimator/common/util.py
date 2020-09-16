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
import yaml

from storage_estimator.dfs_sb import VOS_SIZE, get_dfs_sb_obj
from storage_estimator.vos_size import MetaOverhead
from storage_estimator.vos_structures import Containers


class CommonBase(object):
    def __init__(self):
        self._verbose = False

    def set_verbose(self, verbose):
        self._check_value_type(verbose, bool)
        self._verbose = verbose

    def _check_value_type(self, value, values_type):
        if not isinstance(value, values_type):
            raise TypeError(
                'item {0} must be of type {1}'.format(
                    value, type(values_type)))

    def _error(self, msg):
        print('Error: {0}'.format(msg))

    def _info(self, msg):
        print(msg)

    def _debug(self, msg):
        if self._verbose:
            print('  {}'.format(msg))

    def _debug_append(self, msg):
        if self._verbose:
            print('{}'.format(msg), end='')

    def _get_power_labels(self):
        return {
            0: 'bytes',
            1: 'KiB',
            2: 'MiB',
            3: 'GiB',
            4: 'TiB',
            5: 'PiB',
            6: 'EiB',
            7: 'ZiB',
            8: 'YiB'}

    def _to_human(self, size):
        power_labels = self._get_power_labels()
        power = pow(2, 10)
        n = 0
        while size > power:
            size /= power
            n += 1
        return '{0} {1}'.format(int(size), power_labels[n])

    def _check_suffix(self, string, suffix, pedantic=True):
        if string.endswith(suffix):
            return True
        if pedantic:
            return False
        if string.endswith(suffix.lower()):
            return True
        short_suffix = suffix.replace('i', '')
        if string.endswith(short_suffix):
            return True
        if string.endswith(short_suffix.lower()):
            return True
        shorter_suffix = suffix.replace('iB', '')
        if string.endswith(shorter_suffix):
            return True
        if string.endswith(shorter_suffix.lower()):
            return True
        return False

    def _remove_suffix(self, string, suffix, pedantic=True):
        string = string.replace(suffix, '')
        if pedantic:
            return string

        for letter in suffix:
            string = string.replace(letter, '')
            string = string.replace(letter.lower(), '')

        return string

    def _check_positive_number(self, number):
        self._check_value_type(number, int)
        if number < 1:
            raise ValueError(
                '{0} must be a positive not zero value'.format(number))

    def _from_human(self, human_number):
        self._check_value_type(human_number, str)
        number = human_number
        power_labels = self._get_power_labels()
        for k, v in power_labels.items():
            if self._check_suffix(human_number, v, False):
                number = self._remove_suffix(human_number, v, False)
                number = int(number)
                number = pow(1024, k) * number

        number = int(number)
        self._check_positive_number(number)
        return number


class ProcessBase(CommonBase):
    def __init__(self, args):
        super(ProcessBase, self).__init__()
        self._args = args
        self.set_verbose(args.verbose)
        self._meta = self._get_vos_meta(args)
        self._process_block_values()

    def get_io_size(self):
        return self._io_size

    def get_chunk_size(self):
        return self._chunk_size

    def _parse_num_value(self, key_value, default_value):
        op = vars(self._args)
        value = op.get(key_value, default_value)
        value = self._from_human(value)
        self._check_positive_number(value)
        return value

    def _process_scm_cutoff(self):
        scm_cutoff = self._meta.get('scm_cutoff')

        if 'scm_cutoff' in self._args and self._args.scm_cutoff:
            scm_cutoff = self._parse_num_value('scm_cutoff', '4KiB')
            self._meta['scm_cutoff'] = scm_cutoff

        return scm_cutoff

    def _process_block_values(self):
        scm_cutoff = self._process_scm_cutoff()
        io_size = self._parse_num_value('io_size', '128KiB')
        chunk_size = self._parse_num_value('chunk_size', '1MiB')
        self._debug('using scm_cutoff of {0} bytes'.format(scm_cutoff))
        if io_size % scm_cutoff:
            raise ValueError('io_size must be multiple of scm_cutoff')
        self._debug('using io_size of {0} bytes'.format(io_size))
        if chunk_size % io_size:
            raise ValueError('chunk_size must be multiple of io_size')
        self._debug('using chunk_size of {0} bytes'.format(chunk_size))
        self._scm_cutoff = scm_cutoff
        self._io_size = io_size
        self._chunk_size = chunk_size

    def _set_num_shards(self, args):
        if 'num_shards' in args:
            self._check_positive_number(args.num_shards)
            self._num_shards = args.num_shards
        else:
            self._num_shards = 0

    def _print_destination_file(self, file_name):
        file_name = os.path.normpath(file_name)
        self._debug('Output file: {0}'.format(file_name))

    def _get_yaml_from_dfs(self, fse, use_average=False):
        dfs_sb = get_dfs_sb_obj()

        if use_average:
            dfs = fse.get_dfs_average()
        else:
            dfs = fse.get_dfs()

        container = dfs.get_container()
        container.add_value(dfs_sb)
        containers = Containers()
        containers.add_value(container)
        containers.set_num_shards(self._args.num_shards)

        return containers.dump()

    def _dump_yaml(self, yaml_str):
        return yaml.safe_dump(yaml_str, default_flow_style=False)

    def _load_yaml_from_file(self, file_name):
        self._debug('loading yaml file {0}'.format(file_name))
        try:
            data = yaml.safe_load(open(file_name, 'r'))
        except OSError as err:
            raise Exception(
                'Failed to open file {0} {1}'.format(
                    file_name, err))

        return data

    def _create_file(self, file_name, buf):
        try:
            if not file_name:
                return

            if not file_name.endswith('.yaml'):
                file_name = file_name + '.yaml'

            self._print_destination_file(file_name)
            with open(file_name, 'w') as f:
                f.write(buf)

        except OSError as err:
            raise Exception(
                'Failed to open file {0} {1}'.format(
                    file_name, err))

    def _process_yaml(self, config_yaml):
        num_shards = config_yaml.get('num_shards', 1)
        self._debug('using {0} vos pools'.format(num_shards))

        overheads = MetaOverhead(self._args, num_shards, self._meta)

        overheads.set_scm_cutoff(self._scm_cutoff)

        if 'containers' not in config_yaml:
            raise Exception(
                'No "containers" key in {0}. Nothing to do'.format(
                    self._args.config[0]))

        self._debug('starting analysis')
        if 'average' in self._args and not self._args.average:
            self._debug(
                'for massive file systems, consider using the average "-x" option')

        self._debug('working...')
        for container in config_yaml.get('containers'):
            overheads.load_container(container)

        self._debug('')
        overheads.print_report()

    def _create_vos_meta(self):
        vos_size = VOS_SIZE()
        meta_str = vos_size.get_vos_size_str(self._args.alloc_overhead)

        return meta_str

    def _get_vos_meta(self, args):
        self._meta_str = self._create_vos_meta()
        if 'meta' in args and args.meta:
            meta_yaml = self._load_yaml_from_file(args.meta)
        else:
            meta_yaml = yaml.safe_load(self._meta_str)

        return meta_yaml

    def _process_stats(self, container):
        stats = {
            'objects': 0,
            'dkeys': 0,
            'akeys': 0,
            'values': 0,
            'dkey_size': 0,
            'akey_size': 0,
            'value_size': 0}

        for object in container['objects']:
            stats['objects'] += 1
            for dkey in object['dkeys']:
                stats['dkey_size'] += dkey.get('size', 0)
                stats['dkeys'] += 1
                for akey in dkey['akeys']:
                    stats['akey_size'] += akey.get('size', 0)
                    stats['akeys'] += 1
                    for value in akey['values']:
                        stats['value_size'] += value.get('size', 0)
                        stats['values'] += 1

        return stats

    def _print_summary(self, config_yaml):
        flat_container = {}
        for container in config_yaml.get('containers'):
            flat_container.update(container)

        stats = self._process_stats(flat_container)

        self._debug('')
        self._debug('values: {0}'.format(stats['values']))
        self._debug('akeys: {0}'.format(stats['akeys']))
        self._debug('dkeys: {0}'.format(stats['dkeys']))
        self._debug('objects: {0}'.format(stats['objects']))
        self._debug('dkey_size: {0}'.format(stats['dkey_size']))
        self._debug('akey_size: {0}'.format(stats['akey_size']))
        self._debug('value_size: {0}'.format(stats['value_size']))
        self._debug('')

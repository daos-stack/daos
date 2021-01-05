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


class ObjectClass(CommonBase):
    def __init__(self, args):
        super(ObjectClass, self).__init__()
        self._dir_oclass = self._update_oclass(args, 'dir_oclass', 'S1')
        self._file_oclass = self._update_oclass(args, 'file_oclass', 'SX')
        self.set_verbose(args.verbose)

    def print_pretty_status(self):
        self._debug(
            '{0:<13}{1:<10}{2:<10}{3:<9}{4:<9}{5:<11}'.format(
                'FS Object',
                'OClass',
                '# Targets',
                '# Stripe',
                '# Parity',
                '# Replicas'))
        self._get_pretty_status('File', self._file_oclass)
        self._get_pretty_status('Directory', self._dir_oclass)

    def validate_number_of_shards(self, num_shards):
        min_shards = self._get_min_shards_required(self._dir_oclass)
        if num_shards < min_shards:
            return min_shards

        min_shards = self._get_min_shards_required(self._file_oclass)
        if num_shards < min_shards:
            return min_shards

        return 0

    def validate_chunk_size(self, chunk_size):
        if self.get_dir_parity():
            if chunk_size % self.get_dir_stripe():
                return True

        if self.get_file_parity():
            if chunk_size % self.get_file_stripe():
                return True

        return False

    def is_ec_enabled(self):
        if self.get_dir_parity():
            return True
        if self.get_file_parity():
            return True
        return False

    def get_dir_targets(self):
        return self._get_oclass_parameter(
            self._dir_oclass, 'number_of_targets')

    def get_dir_stripe(self):
        return self._get_oclass_parameter(
            self._dir_oclass, 'number_of_stripe_cells')

    def get_dir_parity(self):
        return self._get_oclass_parameter(
            self._dir_oclass, 'number_of_parity_cells')

    def get_dir_replicas(self):
        return self._get_oclass_parameter(
            self._dir_oclass, 'number_of_replicas')

    def get_file_targets(self):
        return self._get_oclass_parameter(
            self._file_oclass, 'number_of_targets')

    def get_file_stripe(self):
        return self._get_oclass_parameter(
            self._file_oclass, 'number_of_stripe_cells')

    def get_file_parity(self):
        return self._get_oclass_parameter(
            self._file_oclass, 'number_of_parity_cells')

    def get_file_replicas(self):
        return self._get_oclass_parameter(
            self._file_oclass, 'number_of_replicas')

    def get_supported_oclass(self):
        return self._get_oclass_definitions().keys()

    def _get_min_shards_required(self, oclass_type):
        parity = self._get_oclass_parameter(
            oclass_type, 'number_of_parity_cells')
        stripe = self._get_oclass_parameter(
            oclass_type, 'number_of_stripe_cells')
        targets = self._get_oclass_parameter(oclass_type, 'number_of_targets')
        replicas = self._get_oclass_parameter(
            oclass_type, 'number_of_replicas')

        return max(stripe + parity, targets, replicas)

    def _get_pretty_status(self, label, oclass_type):
        targets = self._get_oclass_parameter(oclass_type, 'number_of_targets')

        if targets == 0:
            targets = 'all'

        cells = self._get_oclass_parameter(
            oclass_type, 'number_of_stripe_cells')
        parity = self._get_oclass_parameter(
            oclass_type, 'number_of_parity_cells')
        replicas = self._get_oclass_parameter(
            oclass_type, 'number_of_replicas')

        self._debug('{0:<13}{1:<10}{2:<10}{3:<9}{4:<9}{5:<11}'.format(
            label, oclass_type, targets, cells, parity, replicas))

    def _update_oclass(self, args, key_value, default_value):
        op = vars(args)

        value = op.get(key_value)

        supported_oclasses = self._get_oclass_definitions()

        if value not in supported_oclasses:
            raise ValueError(
                'unknown object class "{0}", the supported objects are {1}:'.format(
                    value, self.get_supported_oclass()))

        return value

    def _get_oclass_parameter(self, oclass_type, parameter_id):
        ec_parameters = {
            'number_of_targets': 0,
            'number_of_stripe_cells': 1,
            'number_of_parity_cells': 2,
            'number_of_replicas': 3
        }
        return self._get_oclass_definitions(
        )[oclass_type][ec_parameters[parameter_id]]

    def _get_oclass_definitions(self):
        return {
            # S1 : shards=1, S2 means shards=2, ...
            # SX : spreading across all targets within the pool
            'S1': (1, 1, 0, 1),
            'S2': (2, 1, 0, 1),
            'S4': (4, 1, 0, 1),
            'S8': (8, 1, 0, 1),
            'SX': (0, 1, 0, 1),
            # 2-way replicated object, it spreads across all targets within the
            # pool
            'RP_2GX': (0, 1, 0, 2),
            # 3-way replicated object, it spreads across all targets within the
            # pool
            'RP_3GX': (0, 1, 0, 3),
            # 8+2 Erasure Coded object, it spreads across all targets within
            # the pool
            'EC_8P2GX': (0, 8, 2, 1),
            # 16+2 Erasure Coded object, it spreads across all targets within
            # the pool
            'EC_16P2GX': (0, 16, 2, 1)
        }


class Common(CommonBase):
    def __init__(self, args):
        self._args = args
        self.set_verbose(args.verbose)
        self._meta = self._get_vos_meta()

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

    def _get_vos_meta(self):
        self._meta_str = self._create_vos_meta()

        return yaml.safe_load(self._meta_str)

    def _create_vos_meta(self):
        vos_size = VOS_SIZE()
        meta_str = vos_size.get_vos_size_str(self._args.alloc_overhead)

        return meta_str

    def _print_destination_file(self, file_name):
        file_name = os.path.normpath(file_name)
        self._debug('Output file: {0}'.format(file_name))

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

    def _process_yaml(self, config_yaml):
        num_shards = config_yaml.get('num_shards', 1)
        self._debug('using {0} vos pools'.format(num_shards))

        overheads = MetaOverhead(self._args, num_shards, self._meta)

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


class ProcessBase(Common):
    def __init__(self, args):
        super(ProcessBase, self).__init__(args)
        self._oclass = ObjectClass(args)
        self._process_block_values()
        self._process_checksum()
        self._num_shards = self._get_num_shards(args)
        self._meta = self._update_vos_meta(args)

    def get_io_size(self):
        return self._io_size

    def get_chunk_size(self):
        return self._chunk_size

    def _update_vos_meta(self, args):
        if args.meta:
            return self._load_yaml_from_file(args.meta)

        return self._meta

    def _get_num_shards(self, args):
        num_shards = args.num_shards
        shards_required = self._oclass.validate_number_of_shards(num_shards)
        if shards_required > 0:
            raise ValueError(
                'Insufficient shards. Wanted {0} given {1}'.format(
                    shards_required, num_shards))

        return num_shards

    def _parse_num_value(self, key_value, default_value):
        op = vars(self._args)
        value = op.get(key_value, default_value)
        value = self._from_human(value)
        self._check_positive_number(value)
        return value

    def _process_scm_cutoff(self):
        scm_cutoff = self._meta.get('scm_cutoff')

        if self._args.scm_cutoff:
            scm_cutoff = self._parse_num_value('scm_cutoff', '4KiB')
            self._meta['scm_cutoff'] = scm_cutoff

        return scm_cutoff

    def _process_checksum(self):
        self._csum_size = 0

        csummers = self._meta.get('csummers')

        if self._args.checksum:
            csum_name = self._args.checksum

            if csum_name not in csummers:
                raise ValueError(
                    'unknown checksum algorithm: "{0}", the supported checksum algorithms are: {1}'. format(
                        csum_name, csummers.keys()))

            csum_size = csummers[csum_name]
            self._debug(
                'using checksum "{0}" algorithm of size {1} bytes'.format(
                    csum_name, csum_size))
            self._csum_size = csum_size

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

        if self._oclass.validate_chunk_size(chunk_size):
            raise ValueError(
                'chunk_size must be multiple of number of stripes')

        self._debug('using chunk_size of {0} bytes'.format(chunk_size))
        self._scm_cutoff = scm_cutoff
        self._io_size = io_size
        self._chunk_size = chunk_size
        self._oclass.print_pretty_status()

    def _get_yaml_from_dfs(self, fse, use_average=False):
        dfs_sb = get_dfs_sb_obj()

        if use_average:
            dfs = fse.get_dfs_average()
        else:
            dfs = fse.get_dfs()

        container = dfs.get_container()
        container.add_value(dfs_sb)
        container.set_csum_size(self._csum_size)
        container.set_csum_gran(self._chunk_size)

        containers = Containers()
        containers.add_value(container)
        containers.set_num_shards(self._num_shards)

        return containers.dump()

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

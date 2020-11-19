#!/usr/bin/env python

from __future__ import print_function
import argparse
import sys

from storage_estimator.dfs_sb import get_dfs_example, print_daos_version, get_dfs_inode_akey
from storage_estimator.parse_csv import ProcessCSV
from storage_estimator.explorer import FileSystemExplorer
from storage_estimator.util import ProcessBase

tool_description = '''DAOS estimation tool
This CLI is able to estimate the SCM/NVMe ratios
'''


class CreateExample(ProcessBase):
    def __init__(self, args):
        super(CreateExample, self).__init__(args)

    def run(self):
        self._info('Vos metadata overhead:')
        self._create_file(self._args.meta_out, self._meta_str)
        config_yaml = get_dfs_example()
        self._create_file(self._args.dfs_file_name, config_yaml)


def create_dfs_example(args):
    print_daos_version()
    try:
        example = CreateExample(args)
        example.run()

    except Exception as err:
        print('Error: {0}'.format(err))
        sys.exit(-1)


class ProcessFS(ProcessBase):
    def __init__(self, args):
        super(ProcessFS, self).__init__(args)

    def run(self):
        fse = self._get_estimate_from_fs()
        config_yaml = self._get_yaml_from_dfs(fse, args.average)
        yaml_str = self._dump_yaml(config_yaml)
        self._create_file(args.output, yaml_str)
        self._process_yaml(config_yaml)

    def _get_estimate_from_fs(self):
        inode_akey = get_dfs_inode_akey()
        fse = FileSystemExplorer(args.path[0])
        fse.set_verbose(args.verbose)
        fse.set_io_size(self.get_io_size())
        fse.set_chunk_size(self.get_chunk_size())
        fse.set_dfs_inode(inode_akey)
        fse.explore()
        fse.print_stats()

        return fse


def process_fs(args):
    try:
        print_daos_version()
        pfs = ProcessFS(args)
        pfs.run()

    except Exception as err:
        print('Error: {0}'.format(err))
        sys.exit(-1)


class ProcessYAML(ProcessBase):
    def __init__(self, args):
        super(ProcessYAML, self).__init__(args)

    def run(self):
        config_yaml = self._load_yaml_from_file(args.config[0])
        self._process_yaml(config_yaml)


def process_yaml(args):
    try:
        print_daos_version()
        yaml = ProcessYAML(args)
        yaml.run()

    except Exception as err:
        print('Error: {0}'.format(err))
        sys.exit(-1)


def process_csv(args):
    try:
        print_daos_version()
        csv = ProcessCSV(args)
        csv.run()
    except Exception as err:
        print('Error: {0}'.format(err))
        sys.exit(-1)


# create the top-level parser
parser = argparse.ArgumentParser(description=tool_description)
subparsers = parser.add_subparsers(description='valid subcommands')

example_description = '''
The "create_example" command creates the "vos_dfs_sample.yaml" and the
"vos_size.yaml" yaml files. These files can be used as inputs by the
commands "explore_fs", "read_yaml", "read_csv".
'''

# create the parser for the create_example command
example = subparsers.add_parser(
    'create_example',
    help='Create a YAML example of the DFS layout',
    description=example_description)
example.add_argument('-a', '--alloc_overhead', type=int,
                     help='Vos alloc overhead', default=16)
example.add_argument(
    '-f',
    '--dfs_file_name',
    type=str,
    help='Output file name of the DFS example',
    default='vos_dfs_sample.yaml')
example.add_argument(
    '-m',
    '--meta_out',
    type=str,
    help='Output file name of the Vos Metadata',
    default='vos_size.yaml')
example.add_argument(
    '-v',
    '--verbose',
    action='store_true',
    help='Explain what is being done')
example.set_defaults(func=create_dfs_example)


# read the file system
explore = subparsers.add_parser(
    'explore_fs', help='Estimate the VOS overhead from a given tree directory')
explore.add_argument(
    'path',
    type=str,
    nargs=1,
    help='Path to the target directory',
    default='')
explore.add_argument(
    '-v',
    '--verbose',
    action='store_true',
    help='Explain what is being done')
explore.add_argument(
    '-x',
    '--average',
    action='store_true',
    help='Use average file size for estimation. (Faster)')
explore.add_argument(
    '-i',
    '--io_size',
    type=str,
    help='I/O size.',
    default='128KiB')
explore.add_argument(
    '-c',
    '--chunk_size',
    type=str,
    help='Chunk size. Must be multiple of I/O size',
    default='1MiB')
explore.add_argument(
    '-s',
    '--scm_cutoff',
    type=str,
    help='SCM threshold in bytes, optional suffixes KiB, MiB, ..., YiB',
    default='')
explore.add_argument(
    '-n',
    '--num_shards',
    type=int,
    help='Number of VOS Pools',
    default=1000)
explore.add_argument('-a', '--alloc_overhead', type=int,
                     help='Vos alloc overhead', default=16)
explore.add_argument(
    '-k',
    '--checksum',
    type=str,
    help='[optional] Checksum algorithm to be used crc16, crc32, crc64, sha1, sha256, sha512',
    default='')
explore.add_argument(
    '-m',
    '--meta',
    metavar='META',
    help='[optional] Input metadata file',
    default='')
explore.add_argument(
    '-o',
    '--output',
    dest='output',
    type=str,
    help='Output file name',
    default='')

explore.set_defaults(func=process_fs)

# parse a yaml file
yaml_file = subparsers.add_parser(
    'read_yaml', help='Estimate the VOS overhead from a given YAML file')
yaml_file.add_argument(
    '-v',
    '--verbose',
    action='store_true',
    help='Explain what is being done')
yaml_file.add_argument('config', metavar='CONFIG', type=str, nargs=1,
                       help='Path to the input yaml configuration file')
yaml_file.add_argument('-a', '--alloc_overhead', type=int,
                       help='Vos alloc overhead', default=16)
yaml_file.add_argument(
    '-s',
    '--scm_cutoff',
    type=str,
    help='SCM threshold in bytes, optional suffixes KiB, MiB, ..., YiB',
    default='')
yaml_file.add_argument(
    '-n',
    '--num_shards',
    type=int,
    help='Number of VOS Pools',
    default=1000)
yaml_file.add_argument(
    '-m',
    '--meta',
    metavar='META',
    help='[optional] Input metadata file',
    default='')
yaml_file.set_defaults(func=process_yaml)

# parse a csv file
csv_file = subparsers.add_parser(
    'read_csv', help='Estimate the VOS overhead from a given CSV file')
csv_file.add_argument(
    'csv',
    metavar='CSV',
    type=str,
    nargs=1,
    help='Input CSV file (assumes Argonne format)')
csv_file.add_argument(
    '--file_name_size',
    type=int,
    dest='file_name_size',
    help='Average file name length',
    default=32)
csv_file.add_argument(
    '-i',
    '--io_size',
    type=str,
    help='I/O size.',
    default='128KiB')
csv_file.add_argument(
    '--chunk_size',
    dest='chunk_size',
    type=str,
    help='Array chunk size. Must be multiple of I/O size',
    default='1MiB')
csv_file.add_argument(
    '-s',
    '--scm_cutoff',
    type=str,
    help='SCM threshold in bytes, optional suffixes KiB, MiB, ..., YiB',
    default='')
csv_file.add_argument(
    '--num_shards',
    dest='num_shards',
    type=int,
    help='Number of vos pools',
    default=1000)
csv_file.add_argument(
    '-k',
    '--checksum',
    type=str,
    help='[optional] Checksum algorithm to be used crc16, crc32, crc64, sha1, sha256, sha512',
    default='')
csv_file.add_argument(
    '-v',
    '--verbose',
    action='store_true',
    help='Explain what is being done')
csv_file.add_argument('-a', '--alloc_overhead', type=int,
                      help='Vos alloc overhead', default=16)
csv_file.add_argument(
    '-m',
    '--meta',
    metavar='META',
    help='[optional] Input metadata file',
    default='')
csv_file.add_argument(
    '-o', '--output',
    dest='output',
    type=str,
    help='Output file name',
    default='')
csv_file.set_defaults(func=process_csv)

# parse the args and call whatever function was selected
args = parser.parse_args()
args.func(args)

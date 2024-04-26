"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import sys
from argparse import ArgumentParser

from util.io_utilities import DirTree


def _populate_dir_tree(path, height, subdirs_per_node, files_per_node, needles, prefix):
    """Create a directory tree and its needle files.

    Args:
        path (str): path under which the directory tree will be created
        height (int): height of the directory tree
        subdirs_per_node (int): number of subdirectories per directory
        files_per_node (int): number of files created per directory
        needles (int): number of needles
        prefix (str): needle prefix
    """
    print(f'Populating: {path}')
    dir_tree = DirTree(path, height, subdirs_per_node, files_per_node)
    dir_tree.set_needles_prefix(prefix)
    dir_tree.set_number_of_needles(needles)
    tree_path = dir_tree.create()
    print(f'Directory tree created at: {tree_path}')


def main():
    """Run the program."""
    parser = ArgumentParser(prog='directory_tree_test.py', description='Create a directory tree')
    parser.add_argument('-p', '--path', type=str, help='path for the directory tree')
    parser.add_argument('-h', '--height', type=int, help='height of the directory tree')
    parser.add_argument('-s', '--subdirs', type=int, help='number of subdirectories per directory')
    parser.add_argument('-f', '--files', type=int, help='number of files created per directory')
    parser.add_argument('-n', '--needles', type=int, help='number of files in the bottom directory')
    parser.add_argument('-pr', '--prefix', type=str, help='bottom directory file prefix')
    args = parser.parse_args()

    try:
        _populate_dir_tree(
            args.path, args.height, args.subdirs, args.files, args.needles, args.prefix)
    except Exception as error:      # pylint: disable=broad-except
        print(f'Error detected: {str(error)}')
        sys.exit(1)
    sys.exit(0)


if __name__ == '__main__':
    main()

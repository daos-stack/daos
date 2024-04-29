"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import logging
import os
import random
import shutil
import sys
import tempfile
from argparse import ArgumentParser

from util.logger_utils import get_console_handler


class DirTree():
    """This class creates a directory-tree.

    The height, the number of files and subdirectories that will be created to populate the
    directory-tree are configurable. The name of the directories and files are randomly generated.
    The files include the suffix ".file". The class has the option to create a configurable number
    of files at the very bottom of the directory-tree with the suffix ".needle"

    Examples:
        tree = DirTree("/mnt", height=7, subdirs_per_node=4, files_per_node=5)
        tree.create()

            It will create:
            1 + 4 + 16 + 64 + 256 + 1024 + 4096 + 16384 = 21845 directories
            5 + 20 + 80 + 320 + 1280 + 5120 + 20480 = 27305 files

        tree = DirTree("/mnt", height=2, subdirs_per_node=3, files_per_node=5)
        tree.create()

            It will create:
            1 + 3 + 9 = 13 directories
            5 + 15 = 20 files
    """

    def __init__(self, root, height=1, subdirs_per_node=1, files_per_node=1):
        """Initialize a DirTree object.

        Args:
            root (str): The path where the directory-tree will be created.
            height (int): Height of the directory-tree.
            subdirs_per_node (int): Number of sub directories per directories.
            files_per_node (int): Number of files created per directory.
        """
        self._root = root
        self._subdirs_per_node = subdirs_per_node
        self._height = height
        self._files_per_node = files_per_node
        self._tree_path = ""
        self._needles_prefix = ""
        self._needles_count = 0
        self._needles_paths = []

    def create(self):
        """Populate the directory-tree.

        This method must be called before using the other methods.
        """
        if not self._tree_path:

            try:
                self._tree_path = tempfile.mkdtemp(dir=self._root)
                logger.info("Directory-tree root: %s", self._tree_path)
                self._create_dir_tree(self._tree_path, self._height)
                self._created_remaining_needles()
            except Exception as err:
                raise RuntimeError(f"Failed to populate tree directory with error: {err}") from err

        return self._tree_path

    def destroy(self):
        """Remove the tree directory."""
        if self._tree_path:
            shutil.rmtree(self._tree_path)
            self._tree_path = ""
            self._needles_paths = []
            self._needles_count = 0

    def set_number_of_needles(self, num):
        """Set the number of files that will be created at the very bottom of the directory-tree.

        These files will have the ".needle" suffix.
        """
        self._needles_count = num

    def set_needles_prefix(self, prefix):
        """Set the needle prefix name.

        The file name will begin with that prefix.
        """
        self._needles_prefix = prefix

    def get_probe(self):
        """Get a randomly selected needle file and its absolute path-name.

        Returns:
            tuple: a needle file name randomly selected (str) and the absolute path-name of that
                file (str).
        """
        if not self._needles_paths:
            raise ValueError(f"{self.__class__.__name__} object is not initialized")

        needle_path = random.choice(self._needles_paths)  # nosec
        needle_name = os.path.basename(needle_path)
        return needle_name, needle_path

    def _create_dir_tree(self, current_path, current_height):
        """Create the actual directory tree using depth-first search approach.

        Args:
            current_path (str): current path
            current_height (int): current path height
        """
        if current_height <= 0:
            return

        self._create_needle(current_path, current_height)

        # create files
        for _ in range(self._files_per_node):
            fd, _ = tempfile.mkstemp(dir=current_path, suffix=".file")
            os.close(fd)

        # create nested directories
        for _ in range(self._subdirs_per_node):
            new_path = tempfile.mkdtemp(dir=current_path)
            self._create_dir_tree(new_path, current_height - 1)

    def _created_remaining_needles(self):
        """Create the remaining needle files at random directories at the bottom of the tree.

        If the number of needle files requested is bigger than the number of directories at the
        bottom of the directory tree.
        """
        if self._needles_count <= 0:
            return

        for count in range(self._needles_count):
            new_path = os.path.dirname(random.choice(self._needles_paths))  # nosec
            suffix = f"_{count:05d}.needle"
            fd, _ = tempfile.mkstemp(dir=new_path, prefix=self._needles_prefix, suffix=suffix)
            os.close(fd)

    def _create_needle(self, current_path, current_height):
        """Create a *.needle file if we reach the bottom of the tree.

        Args:
            current_path (str): current path
            current_height (int): current path height
        """
        if current_height != 1:
            return

        if self._needles_count <= 0:
            return

        self._needles_count -= 1
        suffix = "_{:05d}.needle".format(self._needles_count)
        fd, file_name = tempfile.mkstemp(
            dir=current_path, prefix=self._needles_prefix, suffix=suffix)
        os.close(fd)

        self._needles_paths.append(file_name)


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
    logger.info('Populating: %s', path)
    dir_tree = DirTree(path, height, subdirs_per_node, files_per_node)
    dir_tree.set_needles_prefix(prefix)
    dir_tree.set_number_of_needles(needles)
    tree_path = dir_tree.create()
    logger.info('Directory tree created at: %s', tree_path)


def main():
    """Run the program."""
    parser = ArgumentParser(prog='directory_tree.py', description='Create a directory tree')
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
        logger.error('Error detected: %s', str(error))
        logger.debug("Stacktrace", exc_info=True)
        sys.exit(1)
    sys.exit(0)


if __name__ == '__main__':
    logger = logging.getLogger(__name__)
    logger.setLevel(logging.DEBUG)
    logger.addHandler(get_console_handler("%(message)s", logging.DEBUG))
    main()
else:
    logger = logging.getLogger()

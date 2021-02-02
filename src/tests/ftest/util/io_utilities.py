#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from __future__ import print_function

from general_utils import get_random_string, DaosTestError
from pydaos.raw import DaosApiError

import time
import tempfile
import shutil
import os
import random


class DirTree(object):
    """
    This class creates a directory-tree. The height, the number of files and
    subdirectories that will be created to populate the directory-tree are
    configurable.
    The name of the directories and files are randomly generated. The files
    include the suffix ".file".
    The class has the option to create a configurable number of files at the
    very bottom of the directory-tree with the suffix ".needle"

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
        """
        Parameters:
            root             (str): The path where the directory-tree
                                    will be created.
            height           (int): Height of the directory-tree.
            subdirs_per_node (int): Number of sub directories per directories.
            files_per_node   (int): Number of files created per directory.
        """
        self._root = root
        self._subdirs_per_node = subdirs_per_node
        self._height = height
        self._files_per_node = files_per_node
        self._tree_path = ""
        self._needles_prefix = ""
        self._needles_count = 0
        self._needles_paths = []
        self._logger = None

    def create(self):
        """
        Populate the directory-tree. This method must be called before using
        the other methods.
        """
        if self._tree_path:
            return

        try:
            self._tree_path = tempfile.mkdtemp(dir=self._root)
            self._log("Directory-tree root: {0}".format(self._tree_path))
            self._create_dir_tree(self._tree_path, self._height)
            self._created_remaining_needles()
        except Exception as err:
            raise RuntimeError(
                "Failed to populate tree directory with error: {0}".format(err))

        return self._tree_path

    def destroy(self):
        """
        Remove the tree directory.
        """
        if self._tree_path:
            shutil.rmtree(self._tree_path)
            self._tree_path = ""
            self._needles_paths = []
            self._needles_count = 0

    def set_number_of_needles(self, num):
        """
        Set the number of files that will be created at the very bottom of
        the directory-tree. These files will have the ".needle" suffix.
        """
        self._needles_count = num

    def set_needles_prefix(self, prefix):
        """
        Set the needle prefix name. The file name will begin with that prefix.
        """
        self._needles_prefix = prefix

    def get_probe(self):
        """
        Returns a tuple containing a needle file name randomly selected and the
        absolute pathname of that file, in that order.
        """
        if not self._needles_paths:
            raise ValueError(
                "{0} object is not initialized".format(
                    self.__class__.__name__))

        needle_path = random.choice(self._needles_paths)
        needle_name = os.path.basename(needle_path)
        return needle_name, needle_path

    def set_logger(self, fn):
        """
        Set the function that will be used to print log messages.
        If this value is not set, it will work silently.

        Parameters:
            fn (function): Function to be used for logging.
        """
        self._logger = fn

    def _log(self, msg):
        """If logger function is set, print log messages"""
        if self._logger:
            self._logger(msg)

    def _create_dir_tree(self, current_path, current_height):
        """
        Create the actual directory tree using depth-first search approach.
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
        """
        If the number of needle files requested is bigger than the number of
        directories at the bottom of the directory tree. Create the remaining
        needle files at random directories at the bottom of the tree.
        """
        if self._needles_count <= 0:
            return

        for count in range(self._needles_count):
            new_path = os.path.dirname(random.choice(self._needles_paths))
            suffix = "_{:05d}.needle".format(count)
            fd, _ = tempfile.mkstemp(
                dir=new_path, prefix=self._needles_prefix, suffix=suffix)
            os.close(fd)

    def _create_needle(self, current_path, current_height):
        """If we reach the bottom of the tree, create a *.needle file"""
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


def continuous_io(container, seconds):
    """Perform a combination of reads/writes for the specified time period.

    Args:
        container (DaosContainer): container in which to write the data
        seconds (int): how long to write data

    Returns:
        int: number of bytes written to the container

    Raises:
        ValueError: if a data mismatch is detected

    """
    finish_time = time.time() + seconds
    oid = None
    total_written = 0
    size = 500

    while time.time() < finish_time:
        # make some stuff up
        dkey = get_random_string(5)
        akey = get_random_string(5)
        data = get_random_string(size)

        # write it then read it back
        oid = container.write_an_obj(data, size, dkey, akey, oid, 5)
        data2 = container.read_an_obj(size, dkey, akey, oid)

        # verify it came back correctly
        if data != data2.value:
            raise ValueError("Data mismatch in ContinousIo")

        # collapse down the committed epochs
        container.consolidate_epochs()

        total_written += size

    return total_written


def write_until_full(container):
    """Write until we get enospace back.

    Args:
        container (DaosContainer): container in which to write the data

    Returns:
        int: number of bytes written to the container

    """
    total_written = 0
    size = 2048
    _oid = None

    try:
        while True:
            # make some stuff up and write
            dkey = get_random_string(5)
            akey = get_random_string(5)
            data = get_random_string(size)

            _oid = container.write_an_obj(data, size, dkey, akey)
            total_written += size

            # collapse down the committed epochs
            container.slip_epoch()

    except ValueError as exp:
        print(exp)

    return total_written


def write_quantity(container, size_in_bytes):
    """Write a specific number of bytes.

    Note:
        The minimum amount that will be written is 2048 bytes.

    Args:
        container (DaosContainer): which container to write to, it should be in
            an open state prior to the call
        size_in_bytes (int): total number of bytes to be written, although no
            less that 2048 will be written.

    Returns:
        int: number of bytes written to the container

    """
    total_written = 0
    size = 2048
    _oid = None

    try:
        while total_written < size_in_bytes:

            # make some stuff up and write
            dkey = get_random_string(5)
            akey = get_random_string(5)
            data = get_random_string(size)

            _oid = container.write_an_obj(data, size, dkey, akey)
            total_written += size

            # collapse down the committed epochs
            container.slip_epoch()

    except ValueError as exp:
        print(exp)

    return total_written


def write_single_objects(
        container, obj_qty, rec_qty, akey_size, dkey_size, data_size, rank,
        object_class, log=None):
    """Write random single objects to the container.

    Args:
        container (DaosContainer): the container in which to write objects
        obj_qty (int): the number of objects to create in the container
        rec_qty (int): the number of records to create in each object
        akey_size (int): the akey length
        dkey_size (int): the dkey length
        data_size (int): the length of data to write in each record
        rank (int): the server rank to which to write the records
        log (DaosLog|None): object for logging messages

    Returns:
        list: a list of dictionaries containing the object, transaction
            number, and data written to the container

    Raises:
        DaosTestError: if an error is detected writing the objects or
            verifying the write of the objects

    """
    if log:
        log.info("Creating objects in the container")
    object_list = []
    for index in range(obj_qty):
        object_list.append({"obj": None, "record": []})
        for _ in range(rec_qty):
            akey = get_random_string(
                akey_size,
                [record["akey"] for record in object_list[index]["record"]])
            dkey = get_random_string(
                dkey_size,
                [record["dkey"] for record in object_list[index]["record"]])
            data = get_random_string(data_size)
            object_list[index]["record"].append(
                {"akey": akey, "dkey": dkey, "data": data})

            # Write single data to the container
            try:
                (object_list[index]["obj"]) = \
                    container.write_an_obj(
                        data, len(data), dkey, akey, object_list[index]["obj"],
                        rank, object_class)
            except DaosApiError as error:
                raise DaosTestError(
                    "Error writing data (dkey={}, akey={}, data={}) to "
                    "the container: {}".format(dkey, akey, data, error))

            # Verify the single data was written to the container
            data_read = read_single_objects(
                container, data_size, dkey, akey, object_list[index]["obj"])
            if data != data_read:
                raise DaosTestError(
                    "Written data confirmation failed:"
                    "\n  wrote: {}\n  read:  {}".format(data, data_read))

    return object_list


def read_single_objects(container, size, dkey, akey, obj):
    """Read data from the container.

    Args:
        container (DaosContainer): the container from which to read objects
        size (int): amount of data to read
        dkey (str): dkey used to access the data
        akey (str): akey used to access the data
        obj (object): object to read
        txn (int): transaction number

    Returns:
        str: data read from the container

    Raises:
        DaosTestError: if an error is detected reading the objects

    """
    try:
        data = container.read_an_obj(size, dkey, akey, obj)
    except DaosApiError as error:
        raise DaosTestError(
            "Error reading data (dkey={}, akey={}, size={}) from the "
            "container: {}".format(dkey, akey, size, error))
    return data.value


def write_array_objects(
        container, obj_qty, rec_qty, akey_size, dkey_size, data_size, rank,
        object_class, log=None):
    """Write array objects to the container.

    Args:
        container (DaosContainer): the container in which to write objects
        obj_qty (int): the number of objects to create in the container
        rec_qty (int): the number of records to create in each object
        akey_size (int): the akey length
        dkey_size (int): the dkey length
        data_size (int): the length of data to write in each record
        rank (int): the server rank to which to write the records
        log (DaosLog|None): object for logging messages

    Returns:
        list: a list of dictionaries containing the object, transaction
            number, and data written to the container

    Raises:
        DaosTestError: if an error is detected writing the objects or
            verifying the write of the objects

    """
    if log:
        log.info("Creating objects in the container")
    object_list = []
    for index in range(obj_qty):
        object_list.append({"obj": None, "record": []})
        for _ in range(rec_qty):
            akey = get_random_string(
                akey_size,
                [record["akey"] for record in object_list[index]["record"]])
            dkey = get_random_string(
                dkey_size,
                [record["dkey"] for record in object_list[index]["record"]])
            data = [get_random_string(data_size) for _ in range(data_size)]
            object_list[index]["record"].append(
                {"akey": akey, "dkey": dkey, "data": data})

            # Write the data to the container
            try:
                object_list[index]["obj"] = \
                    container.write_an_array_value(
                        data, dkey, akey, object_list[index]["obj"], rank,
                        object_class)
            except DaosApiError as error:
                raise DaosTestError(
                    "Error writing data (dkey={}, akey={}, data={}) to "
                    "the container: {}".format(dkey, akey, data, error))

            # Verify the data was written to the container
            data_read = read_array_objects(
                container, data_size, data_size + 1, dkey, akey,
                object_list[index]["obj"])
            if data != data_read:
                raise DaosTestError(
                    "Written data confirmation failed:"
                    "\n  wrote: {}\n  read:  {}".format(data, data_read))

    return object_list


def read_array_objects(container, size, items, dkey, akey, obj):
    """Read data from the container.

    Args:
        container (DaosContainer): the container from which to read objects
        size (int): number of arrays to read
        items (int): number of items in each array to read
        dkey (str): dkey used to access the data
        akey (str): akey used to access the data
        obj (object): object to read
        txn (int): transaction number

    Returns:
        str: data read from the container

    Raises:
        DaosTestError: if an error is detected reading the objects

    """
    try:
        data = container.read_an_array(size, items, dkey, akey, obj)
    except DaosApiError as error:
        raise DaosTestError(
            "Error reading data (dkey={}, akey={}, size={}, items={}) "
            "from the container: {}".format(
                dkey, akey, size, items, error))
    return [item[:-1] for item in data]


def get_target_rank_list(daos_object):
    """Get a list of target ranks from a DAOS object.

    Note:
        The DaosObj function called is not part of the public API

    Args:
        daos_object (DaosObj): the object from which to get the list of targets

    Raises:
        DaosTestError: if there is an error obtaining the target list from the
            object

    Returns:
        list: list of targets for the specified object

    """
    try:
        daos_object.get_layout()
        return daos_object.tgt_rank_list
    except DaosApiError as error:
        raise DaosTestError(
            "Error obtaining target list for the object: {}".format(error))

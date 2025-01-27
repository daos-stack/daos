"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import time
from logging import getLogger

from command_utils import ExecutableCommand
from command_utils_base import FormattedParameter
from general_utils import DaosTestError, get_random_bytes
from pydaos.raw import DaosApiError
from run_utils import run_remote


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
        dkey = get_random_bytes(5)
        akey = get_random_bytes(5)
        data = get_random_bytes(size)

        # write it then read it back
        oid = container.write_an_obj(data, size, dkey, akey, oid, 5)
        data2 = container.read_an_obj(size, dkey, akey, oid)

        # verify it came back correctly
        if data != data2.value:
            raise ValueError("Data mismatch in ContinuousIo")

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

    try:
        while True:
            # make some stuff up and write
            dkey = get_random_bytes(5)
            akey = get_random_bytes(5)
            data = get_random_bytes(size)

            container.write_an_obj(data, size, dkey, akey)
            total_written += size

            # collapse down the committed epochs
            container.slip_epoch()

    except ValueError as exp:
        log = getLogger()
        log.info(exp)

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

    try:
        while total_written < size_in_bytes:

            # make some stuff up and write
            dkey = get_random_bytes(5)
            akey = get_random_bytes(5)
            data = get_random_bytes(size)

            container.write_an_obj(data, size, dkey, akey)
            total_written += size

            # collapse down the committed epochs
            container.slip_epoch()

    except ValueError as exp:
        log = getLogger()
        log.info(exp)

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
            akey = get_random_bytes(
                akey_size,
                [record["akey"] for record in object_list[index]["record"]])
            dkey = get_random_bytes(
                dkey_size,
                [record["dkey"] for record in object_list[index]["record"]])
            data = get_random_bytes(data_size)
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
                    "the container: {}".format(
                        dkey, akey, data, error)) from error

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
        dkey (bytes): dkey used to access the data
        akey (bytes): akey used to access the data
        obj (object): object to read
        txn (int): transaction number

    Returns:
        bytes: data read from the container

    Raises:
        DaosTestError: if an error is detected reading the objects

    """
    try:
        data = container.read_an_obj(size, dkey, akey, obj)
    except DaosApiError as error:
        raise DaosTestError(
            "Error reading data (dkey={}, akey={}, size={}) from the "
            "container: {}".format(dkey, akey, size, error)) from error
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
            akey = get_random_bytes(
                akey_size,
                [record["akey"] for record in object_list[index]["record"]])
            dkey = get_random_bytes(
                dkey_size,
                [record["dkey"] for record in object_list[index]["record"]])
            data = [get_random_bytes(data_size) for _ in range(data_size)]
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
                    "the container: {}".format(
                        dkey, akey, data, error)) from error

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
        dkey (bytes): dkey used to access the data
        akey (bytes): akey used to access the data
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
                dkey, akey, size, items, error)) from error
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
            "Error obtaining target list for the object: {}".format(
                error)) from error


class DirectoryTreeCommand(ExecutableCommand):
    """Class defining the DirectoryTreeCommand object."""

    def __init__(self, hosts, namespace="/run/directory_tree/*"):
        """Create a DirectoryTreeCommand object.

        Args:
            hosts (NodeSet): hosts on which to run the command
            namespace (str): command namespace. Defaults to /run/directory_tree/*
        """
        path = os.path.realpath(os.path.join(os.path.dirname(__file__), '..'))
        super().__init__(namespace, "directory_tree.py", path)

        # directory_tree.py options
        self.path = FormattedParameter("--path {}")
        self.height = FormattedParameter("--height {}")
        self.subdirs = FormattedParameter("--subdirs {}")
        self.files = FormattedParameter("--files {}")
        self.needles = FormattedParameter("--needles {}")
        self.prefix = FormattedParameter("--prefix {}")

        # run options
        self.hosts = hosts.copy()
        self.timeout = 180

    def run(self):
        # pylint: disable=arguments-differ
        """Run the command.

        Returns:
            CommandResult: groups of command results from the same hosts with the same return status
        """
        self.log.info('Running directory_tree.py on %s', str(self.hosts))
        return run_remote(self.log, self.hosts, self.with_exports, timeout=self.timeout)

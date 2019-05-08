#!/usr/bin/python
"""
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
"""

from __future__ import print_function

from avocado import fail_on
from daos_api import DaosApiError, DaosServer, DaosContainer, DaosPool
from general_utils import get_random_string

from time import sleep
from os import geteuid, getegid


class DaosUtilityError(Exception):
    """An exception raised by a DAOS utility method."""


@fail_on(DaosApiError)
def get_pool(context, mode, size, name, svcn=1, log=None):
    """Return a DAOS pool that has been created an connected.

    Args:
        context (DaosContext): the context to use to create the pool
        mode (int): the pool mode
        size (int): the size of the pool
        name (str): the name of the pool
        svcn (int): the number of pool replica leaders
        log (DaosLog|None): object for logging messages

    Returns:
        DaosPool: an object representing a DAOS pool

    """
    if log:
        log.info("Creating a pool")
    pool = DaosPool(context)
    pool.create(mode, geteuid(), getegid(), size, name, svcn=svcn)
    if log:
        log.info("Connecting to the pool")
    pool.connect(1 << 1)
    return pool


@fail_on(DaosApiError)
def get_container(context, pool, log=None):
    """Retrun a DAOS a container that has been created an opened.

    Args:
        context (DaosContext): the context to use to create the container
        pool (DaosPool): pool in which to create the container
        log (DaosLog|None): object for logging messages

    Returns:
        DaosContainer: an object representing a DAOS container

    """
    if log:
        log.info("Creating a container")
    container = DaosContainer(context)
    container.create(pool.handle)
    if log:
        log.info("Opening a container")
    container.open()
    return container


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

    """
    if log:
        log.info("Creating objects in the container")
    object_list = []
    for x in range(obj_qty):
        object_list.append({"obj": None, "txn": None, "record": []})
        for _ in range(rec_qty):
            akey = get_random_string(
                akey_size,
                [record["akey"] for record in object_list[x]["record"]])
            dkey = get_random_string(
                dkey_size,
                [record["dkey"] for record in object_list[x]["record"]])
            data = get_random_string(data_size)
            object_list[x]["record"].append(
                {"akey": akey, "dkey": dkey, "data": data})

            # Write single data to the container
            try:
                (object_list[x]["obj"], object_list[x]["txn"]) = \
                    container.write_an_obj(
                        data, len(data), dkey, akey, object_list[x]["obj"],
                        rank, object_class)
            except DaosApiError as error:
                raise DaosUtilityError(
                    "Error writing data (dkey={}, akey={}, data={}) to "
                    "the container: {}".format(dkey, akey, data, error))

            # Verify the single data was written to the container
            data_read = read_single_objects(
                container, data_size, dkey, akey, object_list[x]["obj"],
                object_list[x]["txn"])
            if data != data_read:
                raise DaosUtilityError(
                    "Written data confirmation failed:"
                    "\n  wrote: {}\n  read:  {}".format(data, data_read))

    return object_list


def read_single_objects(container, size, dkey, akey, obj, txn):
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

    """
    try:
        data = container.read_an_obj(size, dkey, akey, obj, txn)
    except DaosApiError as error:
        raise DaosUtilityError(
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

    """
    if log:
        log.info("Creating objects in the container")
    object_list = []
    for x in range(obj_qty):
        object_list.append({"obj": None, "txn": None, "record": []})
        for _ in range(rec_qty):
            akey = get_random_string(
                akey_size,
                [record["akey"] for record in object_list[x]["record"]])
            dkey = get_random_string(
                dkey_size,
                [record["dkey"] for record in object_list[x]["record"]])
            data = [get_random_string(data_size) for _ in range(data_size)]
            object_list[x]["record"].append(
                {"akey": akey, "dkey": dkey, "data": data})

            # Write the data to the container
            try:
                object_list[x]["obj"], object_list[x]["txn"] = \
                    container.write_an_array_value(
                        data, dkey, akey, object_list[x]["obj"], rank,
                        object_class)
            except DaosApiError as error:
                raise DaosUtilityError(
                    "Error writing data (dkey={}, akey={}, data={}) to "
                    "the container: {}".format(dkey, akey, data, error))

            # Verify the data was written to the container
            data_read = read_array_objects(
                container, data_size, data_size + 1, dkey, akey,
                object_list[x]["obj"], object_list[x]["txn"])
            if data != data_read:
                raise DaosUtilityError(
                    "Written data confirmation failed:"
                    "\n  wrote: {}\n  read:  {}".format(data, data_read))

    return object_list


def read_array_objects(container, size, items, dkey, akey, obj, txn):
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

    """
    try:
        data = container.read_an_array(
            size, items, dkey, akey, obj, txn)
    except DaosApiError as error:
        raise DaosUtilityError(
            "Error reading data (dkey={}, akey={}, size={}, items={}) "
            "from the container: {}".format(
                dkey, akey, size, items, error))
    return [item[:-1] for item in data]


@fail_on(DaosApiError)
def kill_server(server_group, context, rank, pool, log=None):
    """Kill a specific server rank.

    Args:
        server_group (str):
        context (DaosContext): the context to use to create the DaosServer
        rank (int): daos server rank to kill
        pool (DaosPool): the DaosPool from which to exclude the rank
        log (DaosLog|None): object for logging messages

    Returns:
        None

    """
    if log:
        log.info("Killing DAOS server {} (rank {})".format(server_group, rank))
    server = DaosServer(context, server_group, rank)
    server.kill(1)
    if log:
        log.info("Excluding server rank {}".format(rank))
    pool.exclude([rank])


@fail_on(DaosApiError)
def get_pool_status(pool, log):
    """Determine if the pool rebuild is complete.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status

    Returns:
        None

    """
    pool_info = pool.pool_query()
    message = "Pool: pi_ntargets={}".format(pool_info.pi_ntargets)
    message += ", pi_nnodes={}".format(
        pool_info.pi_nnodes)
    message += ", pi_ndisabled={}".format(
        pool_info.pi_ndisabled)
    message += ", rs_version={}".format(
        pool_info.pi_rebuild_st.rs_version)
    message += ", rs_done={}".format(
        pool_info.pi_rebuild_st.rs_done)
    message += ", rs_toberb_obj_nr={}".format(
        pool_info.pi_rebuild_st.rs_toberb_obj_nr)
    message += ", rs_obj_nr={}".format(
        pool_info.pi_rebuild_st.rs_obj_nr)
    message += ", rs_rec_nr={}".format(
        pool_info.pi_rebuild_st.rs_rec_nr)
    message += ", rs_errno={}".format(
        pool_info.pi_rebuild_st.rs_errno)
    log.info(message)
    return pool_info


def is_pool_rebuild_complete(pool, log):
    """Determine if the pool rebuild is complete.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status

    Returns:
        bool: pool rebuild completion status

    """
    get_pool_status(pool, log)
    return pool.pool_info.pi_rebuild_st.rs_done == 1


def read_during_rebuild(
        container, pool, log, written_objects, data_size, read_method):
    """Read all the written data while rebuild is still in progress.

    Args:
        container (DaosContainer): the container from which to read objects
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status
        written_objects (list): record data type to write/read
        data_size (int):
        read_method (object): function to call to read the data, e.g.
            read_single_objects or read_array_objects

    Returns:
        None

    Raises:
        DaosUtilityError: if the rebuild completes before the read is complete
            or read errors are detected

    """
    x = 0
    y = 0
    incomplete = True
    failed_reads = False
    while not is_pool_rebuild_complete(pool, log) and incomplete:
        incomplete = x < len(written_objects)
        if incomplete:
            # Read the data from the previously written record
            record_info = {
                "container": container,
                "size": data_size,
                "dkey": written_objects[x]["record"][y]["dkey"],
                "akey": written_objects[x]["record"][y]["akey"],
                "obj": written_objects[x]["obj"],
                "txn": written_objects[x]["txn"]}
            if read_method.__name__ == "read_array_objects":
                record_info["items"] = data_size + 1
            read_data = read_method(**record_info)

            # Verify the data just read matches the original data written
            record_info["data"] = written_objects[x]["record"][y]["data"]
            if record_info["data"] != read_data:
                failed_reads = True
                log.error(
                    "<obj: %s, rec: %s>: Failed reading data "
                    "(dkey=%s, akey=%s):\n  read:  %s\n  wrote: %s",
                    x, y, record_info["dkey"], record_info["akey"],
                    read_data, record_info["data"])
            else:
                log.info(
                    "<obj: %s, rec: %s>: Passed reading data "
                    "(dkey=%s, akey=%s)",
                    x, y, record_info["dkey"], record_info["akey"])

            # Read the next record in this object or the next object
            y += 1
            if y >= len(written_objects[x]["record"]):
                x += 1
                y = 0

    # Verify that all of the objects and records were read successfully
    # while the rebuild was still active
    if incomplete:
        raise DaosUtilityError(
            "Rebuild completed before all the written data could be read")
    elif failed_reads:
        raise DaosUtilityError("Errors detected reading data during rebuild")


def wait_for_rebuild(pool, log, to_start, interval):
    """Wait for the rebuild to start or end.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status
        to_start (bool): whether to wait for rebuild to start or end
        interval (int): number of seconds to wait in between rebuild
            completion checks

    Returns:
        None

    """
    log.info(
        "Waiting for rebuild to %s ...",
        "start" if to_start else "complete")
    while is_pool_rebuild_complete(pool, log) == to_start:
        log.info(
            "  Rebuild %s ...",
            "has not yet started" if to_start else "in progress")
        sleep(interval)

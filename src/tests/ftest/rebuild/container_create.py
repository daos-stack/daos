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

from apricot import TestWithServers
from avocado import fail_on
from daos_api import DaosApiError, DaosServer, DaosContainer, DaosPool
from general_utils import get_random_string

from time import sleep
from os import geteuid, getegid


class ContainerCreate(TestWithServers):
    """Run rebuild tests with DAOS servers and clients.

    :avocado: recursive
    """
    def check_cancel_conditions(self, obj_class):
        """Check for test cases that should be cancelled due to open tickets.

        Args:
            obj_class (int): object class number

        Returns:
            None

        """
        server_qty = len(self.hostlist_servers)
        cancel_conditions = (
            ("DAOS-2410", server_qty == 2 and obj_class == 17),
        )
        for ticket, condition in cancel_conditions:
            if condition:
                self.cancelForTicket(ticket)

    @fail_on(DaosApiError)
    def create_pool(self, mode, size, name, svcn=1):
        """Create a DAOS pool.

        Args:
            mode (int): the pool mode
            size (int): the size of the pool
            name (str): the name of the pool
            svcn (int): the number of pool service replica leaders

        Note:
            Assigns self.pool
        """
        self.d_log.info("Creating a pool")
        self.pool = DaosPool(self.context)
        self.pool.create(mode, geteuid(), getegid(), size, name, svcn=svcn)
        self.d_log.info("Connecting to the pool")
        self.pool.connect(1 << 1)

    @fail_on(DaosApiError)
    def create_container(self):
        """Create a DAOS container.

        Note:
            Assigns self.container
        """
        self.d_log.info("Creating a container")
        container = DaosContainer(self.context)
        container.create(self.pool.handle)
        self.d_log.info("Opening a container")
        container.open()

        return container

    def write_single_objects(
            self, container, obj_qty, rec_qty, akey_size, dkey_size, data_size,
            rank, object_class):
        """Write sngle objects to the container.

        Args:
            container (DaosContainer): the container in which to write objects
            obj_qty (int): the number of objects to create in the container
            rec_qty (int): the number of records to create in each object
            akey_size (int): the akey length
            dkey_size (int): the dkey length
            data_size (int): the length of data to write in each record
            rank (int): the server rank to which to write the records

        Returns:
            list: a list of dictionaries containing the object, transaction
                number, and data written to the container

        """
        self.d_log.info("Creating objects in the container")
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
                    self.fail(
                        "Error writing data (dkey={}, akey={}, data={}) to "
                        "the container: {}".format(dkey, akey, data, error))

                # Verify the single data was written to the container
                data_read = self.read_single_objects(
                    container, data_size, dkey, akey, object_list[x]["obj"],
                    object_list[x]["txn"])
                if data != data_read:
                    message = (
                        "Written data confirmation failed:",
                        "  wrote: {}".format(data),
                        "  read:  {}".format(data_read))
                    self.fail("\n".join(message))
        return object_list

    def read_single_objects(self, container, size, dkey, akey, obj, txn):
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
            self.fail(
                "Error reading data (dkey={}, akey={}, size={}) from the "
                "container: {}".format(dkey, akey, size, error))
        return data.value

    def write_array_objects(
            self, obj_qty, rec_qty, akey_size, dkey_size, data_size, rank,
            object_class):
        """Write array objects to the container.

        Args:
            obj_qty (int): the number of objects to create in the container
            rec_qty (int): the number of records to create in each object
            akey_size (int): the akey length
            dkey_size (int): the dkey length
            data_size (int): the length of data to write in each record
            rank (int): the server rank to which to write the records

        Returns:
            list: a list of dictionaries containing the object, transaction
                number, and data written to the container

        """
        self.d_log.info("Creating objects in the container")
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
                        self.container.write_an_array_value(
                            data, dkey, akey, object_list[x]["obj"], rank,
                            object_class)
                except DaosApiError as error:
                    self.fail(
                        "Error writing data (dkey={}, akey={}, data={}) to "
                        "the container: {}".format(dkey, akey, data, error))

                # Verify the data was written to the container
                data_read = self.read_array_objects(
                    data_size, data_size + 1, dkey, akey,
                    object_list[x]["obj"], object_list[x]["txn"])
                if data != data_read:
                    message = (
                        "Written data confirmation failed:",
                        "  wrote: {}".format(data),
                        "  read:  {}".format(data_read))
                    self.fail("\n".join(message))
        return object_list

    def read_array_objects(self, size, items, dkey, akey, obj, txn):
        """Read data from the container.

        Args:
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
            data = self.container.read_an_array(
                size, items, dkey, akey, obj, txn)
        except DaosApiError as error:
            self.fail(
                "Error reading data (dkey={}, akey={}, size={}, items={}) "
                "from the container: {}".format(
                    dkey, akey, size, items, error))
        return [item[:-1] for item in data]

    @fail_on(DaosApiError)
    def kill_server(self, rank):
        """Kill a specific server rank.

        Args:
            rank (int): daos server rank to kill

        Returns:
            None

        """
        self.d_log.info(
            "Killing DAOS server {} (rank {})".format(self.server_group, rank))
        server = DaosServer(self.context, self.server_group, rank)
        server.kill(1)
        self.d_log.info("Excluding server rank {}".format(rank))
        self.pool.exclude([rank])

    @fail_on(DaosApiError)
    def get_pool_status(self):
        """Determine if the pool rebuild is complete.

        Args:
            pool (DaosPool): pool for which to determine if rebuild is complete

        Returns:
            None

        """
        pool_info = self.pool.pool_query()
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
        self.log.info(message)
        return pool_info

    def is_pool_rebuild_complete(self):
        """Determine if the pool rebuild is complete.

        Args:
            pool (DaosPool): pool for which to determine if rebuild is complete

        Returns:
            bool: pool rebuild completion status

        """
        self.get_pool_status()
        # self.assertTrue(
        #     self.pool.pool_info.pi_rebuild_st.rs_version == 0,
        #     "Error: Pool map version is zero")
        return self.pool.pool_info.pi_rebuild_st.rs_done == 1

    def read_during_rebuild(self, written_objects, data_size, read_method):
        """Read all the written data while rebuild is still in progress.

        Args:
            written_objects (list): record data type to write/read

        Returns:
            None

        """
        x = 0
        y = 0
        incomplete = True
        failed_reads = False
        while not self.is_pool_rebuild_complete() and incomplete:
            incomplete = x < len(written_objects)
            if incomplete:
                # Read the data from the previously written record
                record_info = {
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
                    self.log.error(
                        "<obj: %s, rec: %s>: Failed reading data "
                        "(dkey=%s, akey=%s):\n  read:  %s\n  wrote: %s",
                        x, y, record_info["dkey"], record_info["akey"],
                        read_data, record_info["data"])
                else:
                    self.log.info(
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
            self.fail(
                "Rebuild completed before all the written data could be read")
        elif failed_reads:
            self.fail("Errors detected reading data during rebuild")

    def wait_for_rebuild(self, to_start, interval):
        """Wait for the rebuild to start or end.

        Args:
            to_start (bool): whether to wait for rebuild to start or end
            interval (int): number of seconds to wait in between rebuild
                completion checks

        Returns:
            None

        """
        self.log.info(
            "Waiting for rebuild to %s ...",
            "start" if to_start else "complete")
        while self.is_pool_rebuild_complete() == to_start:
            self.log.info(
                "  Rebuild %s ...",
                "has not yet started" if to_start else "in progress")
            sleep(interval)

    def test_rebuild_cont_create(self):
        """
        Test Description: Test creating a container while rebuild is ongoing.

        :avocado: tags=cont,rebuild,rebuildsimple,rebuildcontainercreate
        """
        pool_mode = self.params.get("pool_mode", "/run/pool/*")
        pool_size = self.params.get("pool_size", "/run/pool/*")
        pool_name = self.params.get("pool_name", "/run/pool/*")
        object_qty = self.params.get("object_qty", "/run/container/*")
        record_qty = self.params.get("record_qty", "/run/container/*")
        akey_size = self.params.get("akey_size", "/run/container/*")
        dkey_size = self.params.get("dkey_size", "/run/container/*")
        data_size = self.params.get("data_size", "/run/container/*")
        obj_class = self.params.get("obj_class", "/run/container/*")
        rank = 1

        self.log.info("Creating a pool")
        self.create_pool(pool_mode, pool_size, pool_name, svcn=2)

        self.log.info("Creating a container")
        self.container = self.create_container()

        self.log.info("Writing single objects")
        written_objects = self.write_single_objects(
            self.container, object_qty, record_qty, akey_size, dkey_size,
            data_size, rank, obj_class)

        # Debug
        self.log.info("Checking pool status prior to rebuild")
        self.get_pool_status()

        # Kill the server
        self.log.info(
            "Killing DAOS server %s (rank %s)", self.server_group, rank)
        self.kill_server(rank)

        # Wait for recovery to start
        self.wait_for_rebuild(True, 1)

        # make 2nd container during rebuild
        self.container2 = self.create_container()

        # wait for rebuild to complete
        self.wait_for_rebuild(False, 1)

        # verify data rebuilt correctly
        for obj_dict in written_objects:
            for rec_dict in obj_dict["record"]:
                read_data = self.read_single_objects(self.container, data_size,
                                                     rec_dict["dkey"],
                                                     rec_dict["akey"],
                                                     obj_dict["obj"],
                                                     obj_dict["txn"])
                if read_data != rec_dict["data"]:
                    self.fail("Data post-rebuild does not match pre-rebuild.\n"
                              "Read data: {0}\n"
                              "Expected {1}".format(read_data,
                                                    rec_dict["data"]))

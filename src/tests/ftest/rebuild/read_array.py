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

from apricot import TestWithServers
from general_utils import get_pool, get_container, kill_server, DaosTestError
from general_utils import get_pool_status, wait_for_rebuild, verify_rebuild
from io_utilities import read_array_objects, read_during_rebuild
from io_utilities import write_array_objects, get_target_rank_list


class ReadArrayTest(TestWithServers):
    """Run rebuild tests with DAOS servers and clients.

    :avocado: recursive
    """

    def test_read_array_during_rebuild(self):
        """Jira ID: DAOS-691

        Test Description:
            Configure 3 or 6 targets with 1 pool with a service leader quantity
            of 2 or 3.  Add 1 container to the pool configured with 2 or 3
            replicas.  Add 10 objects of 10 records each populated with an
            array of 5 values (currently a sufficent amount of data to be read
            fully before rebuild completes) to a specific rank.  Exclude this
            rank and verify that rebuild is initiated.  While rebuild is
            active, confirm that all the objects and records can be read.
            Finally verify that rebuild completes and the pool info indicates
            the correct number of rebuilt objects and records.  In the case of
            3 targets with a pool configured with 3 replicas, rebuild should
            not yield any rebuilt objects or records, but the data should still
            be accassible.

        Use Cases:
            Basic rebuild of container objects of array values with sufficient
            numbers of rebuild targets and no available rebuild targets.

        :avocado: tags=all,medium,full_regression,rebuild,rebuildreadarray
        """
        pool_mode = self.params.get("mode", "/run/pool/*")
        pool_size = self.params.get("size", "/run/pool/*")
        pool_name = self.params.get("setname", "/run/pool/*")
        pool_svcn = self.params.get("svcn", "/run/pool/*")
        object_qty = self.params.get("object_qty", "/run/container/*")
        record_qty = self.params.get("record_qty", "/run/container/*")
        akey_size = self.params.get("akey_size", "/run/container/*")
        dkey_size = self.params.get("dkey_size", "/run/container/*")
        data_size = self.params.get("data_size", "/run/container/*")
        obj_class = self.params.get("obj_class", "/run/container/*")
        rank = self.params.get("rank", "/run/test/*")

        server_qty = len(self.hostlist_servers)
        replica_qty = obj_class - 14
        total_records = object_qty * record_qty * data_size

        # Do not execute any test variants that don't make sense
        if rank > server_qty - 1:
            self.cancel("Canceling test that would kill a non-existent rank")
        if pool_svcn > server_qty:
            self.cancel(
                "Canceling test with a svcn greater than the server quantity")

        # Cancel any tests with tickets already assigned
        if server_qty < replica_qty:
            self.cancelForTicket("DAOS-2410")
        if rank in [1, 2]:
            self.cancelForTicket("DAOS-2434")

        # Create and connect to a pool
        self.log.info("Creating a pool")
        self.pool = get_pool(
            self.context, pool_mode, pool_size, pool_name, pool_svcn,
            self.d_log)

        # Create and open a container
        self.log.info("Creating a container")
        self.container = get_container(self.context, self.pool, self.d_log)

        # Populate the container with data on the rank to be killed
        self.log.info("Writing array objects")
        try:
            written_objects = write_array_objects(
                self.container, object_qty, record_qty, akey_size, dkey_size,
                data_size, rank, obj_class, self.log)
        except DaosTestError as error:
            self.fail(
                "Detected exception while writing data prior to "
                "rebuild: {}".format(str(error)))

        # Log rebuild status prior to killing the server
        self.log.info("Checking pool status prior to rebuild")
        get_pool_status(self.pool, self.log)
        self.log.info(
            "Object Target List: %s",
            get_target_rank_list(written_objects[0]["obj"]))

        # Kill the server
        self.log.info(
            "Killing DAOS server %s (rank %s)", self.server_group, rank)
        kill_server(
            self.server_group, self.context, rank, self.pool, self.d_log)

        # Wait for recovery to start
        wait_for_rebuild(self.pool, self.log, True, 1)

        # Read the objects during rebuild
        message = "Reading the array objects during rebuild"
        self.log.info(message)
        self.d_log.info(message)
        try:
            read_during_rebuild(
                self.container, self.pool, self.log, written_objects,
                data_size, read_array_objects)
        except DaosTestError as error:
            # if pool_svcn
            self.log.warning(
                "Detected exception while reading during rebuild: %s",
                str(error))

        # Confirm rebuild completes
        wait_for_rebuild(self.pool, self.log, False, 1)
        self.log.info("Rebuild completion detected")

        self.log.info(
            "Object Target List: %s",
            get_target_rank_list(written_objects[0]["obj"]))

        # Confirm the number of rebuilt objects reported by the pool query
        errors = verify_rebuild(
            self.pool,
            self.log,
            0 if server_qty == replica_qty else object_qty,
            0 if server_qty == replica_qty else object_qty,
            0 if server_qty == replica_qty else total_records)
        self.assertTrue(len(errors) == 0, "\n".join(errors))
        self.log.info("Test passed")

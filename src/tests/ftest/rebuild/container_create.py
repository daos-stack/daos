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
from apricot import TestWithServers
from avocado import fail_on
from daos_api import DaosApiError, DaosServer, DaosContainer, DaosPool
from general_utils import get_random_string

from daos_utils import (
    get_pool, get_container, get_pool_status, kill_server, write_single_objects,
    read_single_objects, read_during_rebuild, wait_for_rebuild,
    DaosUtilityError)

from time import sleep
from os import geteuid, getegid


class ContainerCreate(TestWithServers):
    """Run rebuild tests with DAOS servers and clients.

    :avocado: recursive
    """

    def test_rebuild_cont_create(self):
        """
        Test Description: Test creating a container while rebuild is ongoing.

        :avocado: tags=cont,rebuild,rebuildsimple,rebuildcontainercreate
        """
        pool_mode = self.params.get("mode", "/run/poolparams/createmode/*")
        pool_size = self.params.get("size", "/run/poolparams/createsize/*")
        pool_name = self.params.get("setname", "/run/poolparams/*")
        object_qty = self.params.get("object_qty", "/run/container/*")
        record_qty = self.params.get("record_qty", "/run/container/*")
        akey_size = self.params.get("akey_size", "/run/container/*")
        dkey_size = self.params.get("dkey_size", "/run/container/*")
        data_size = self.params.get("data_size", "/run/container/*")
        obj_class = self.params.get("obj_class", "/run/container/*")
        svcn = self.params.get("svcn", "/run/poolparams/createsvc/*")
        rank = self.params.get("rank", "/run/testparams/kill_rank/*")

        # set 2nd rank to write to as anything other than the rank we kill
        rebuild_write_rank = (
            rank - 1 if rank > 0 else len(self.hostlist_servers) - 1)

        # Cancel any tests with tickets already assigned
        if rank == 1 or rank == 2:
            self.cancelForTicket("DAOS-2434")

        self.log.info("Creating a pool")
        self.pool = get_pool(self.context, pool_mode, pool_size, pool_name,
                             svcn, self.d_log)

        self.log.info("Creating a container")
        self.container = get_container(self.context, self.pool, self.d_log)

        self.log.info("Writing single objects")
        try:
            written_objects = write_single_objects(self.container, object_qty,
                                                   record_qty, akey_size,
                                                   dkey_size, data_size, rank,
                                                   obj_class, self.d_log)
        except DaosUtilityError as excep:
            self.fail("Failed to write initial objects to pool: "
                      "{0}".format(str(excep)))

        # Debug
        self.log.info("Checking pool status prior to rebuild")
        get_pool_status(self.pool, self.log)

        # Kill the server
        self.log.info(
            "Killing DAOS server %s (rank %s)", self.server_group, rank)
        kill_server(self.server_group, self.context, rank, self.pool,
                    self.d_log)

        # Wait for recovery to start
        wait_for_rebuild(self.pool, self.log, True, 1)

        # make 2nd container during rebuild
        container2 = get_container(self.context, self.pool, self.d_log)

        # write a single object to it
        try:
            rebuild_write = write_single_objects(container2, 1, 1, 5, 5, 10,
                                                 rebuild_write_rank, obj_class)
        except DaosUtilityError as excep:
            self.fail("Failed to write object to second container: "
                      "{0}".format(str(excep)))

        # wait for rebuild to complete
        wait_for_rebuild(self.pool, self.log, False, 1)

        # verify data rebuilt correctly
        for obj_dict in written_objects:
            for rec_dict in obj_dict["record"]:
                read_data = read_single_objects(self.container, data_size,
                                                rec_dict["dkey"],
                                                rec_dict["akey"],
                                                obj_dict["obj"],
                                                obj_dict["txn"])
                if read_data != rec_dict["data"]:
                    self.fail("Data post-rebuild does not match pre-rebuild.\n"
                              "Read data: {0}\n"
                              "Expected data: {1}".format(read_data,
                                                          rec_dict["data"]))
        
        # verify the data written to 2nd container during rebuild is readable
        rebuild_read = read_single_objects(
            container2, 10,
            rebuild_write[0]["record"][0]["dkey"],
            rebuild_write[0]["record"][0]["akey"],
            rebuild_write[0]["obj"],
            rebuild_write[0]["txn"])
        
        if rebuild_read != rebuild_write[0]["record"][0]["data"]:
            self.fail("Data written to second container does not match "
                      "expected data.\n"
                      "Read data: {0}\n"
                      "Expected data: {1}".format(rebuild_read,
                                                  rebuild_write[0]["data"]))

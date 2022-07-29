#!/usr/bin/python3
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_test_base import IorTestBase
from avocado.core.exceptions import TestFail


class RbldContainerCreate(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Rebuild with container creation test cases.
    Test Class Description:
        These rebuild tests verify the ability to create additional containers
        while rebuild is ongoing.
    :avocado: recursive
    """

    def add_containers_during_rebuild(self, qty=10, index=-1):
        """Add containers to a pool while rebuild is still in progress.
        Args:
            qty (int, optional): the number of containers to create
            index (int, optional): the container index to perform write
                during rebuild

        """
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        object_qty = self.params.get("object_qty", "/run/io/*")
        record_qty = self.params.get("record_qty", "/run/io/*")
        data_size = self.params.get("data_size", "/run/io/*")
        akey_size = self.params.get("akey_size", "/run/io/*")
        dkey_size = self.params.get("dkey_size", "/run/io/*")
        cont_oclass = self.params.get("oclass", "/run/io/*")
        count = 0
        self.container = []
        rebuild_done = False
        self.log.info("..Create %s containers and write data during rebuild.",
                      qty)
        while not rebuild_done and count < qty:
            count += 1
            self.log.info(
                "..Creating container %s/%s in pool %s during rebuild",
                count, qty, self.pool.uuid)
            self.container.append(self.get_container(self.pool))
            rebuild_done = self.pool.rebuild_complete()
            self.log.info(
                "..Rebuild status, rebuild_done= %s", rebuild_done)

        self.container[index].object_qty.value = object_qty
        self.container[index].record_qty.value = record_qty
        self.container[index].data_size.value = data_size
        self.container[index].akey_size.value = akey_size
        self.container[index].dkey_size.value = dkey_size
        self.container[index].oclass.value = cont_oclass
        self.container[index].write_objects(rank, cont_oclass)

    def access_container(self, index=-1):
        """Open and close the specified container.
        Args:
            index (int): index of the daos container object to open/close
        Returns:
            bool: was the opening and closing of the container successful

        """
        status = True
        container = self.container[index]
        self.log.info(
            "..Verifying the container %s created during rebuild", container)
        try:
            container.read_objects()
            container.close()
        except TestFail as error:
            self.log.error(
                "##Container %s read failed:", container, exc_info=error)
            status = False
        return status

    def test_rebuild_container_create(self):
        """Jira ID: DAOS-1168.
        Test Description:
            Configure 4 servers and 1 client.
            Test steps:
            (1)Start IOR before rebuild.
            (2)Starting rebuild by killing rank.
            (3)Wait for rebuild to start for race condition.
            (4)Race condition, create containers, data write/read during
               rebuild.
            (5)Wait for rebuild to finish.
            (6)Check for pool and rebuild info after rebuild.
        Use Cases:
            Basic rebuild of container objects of array values with sufficient
            numbers of rebuild targets and no available rebuild targets.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=rebuild
        :avocado: tags=rebuild_cont_create

        """
        # set params
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        ior_loop = self.params.get("ior_test_loop", "/run/ior/*")
        cont_qty = self.params.get("cont_qty", "/run/io/*")
        node_qty = len(self.hostlist_servers)

        # create pool
        self.create_pool()

        # make sure pool looks good before we start
        info_checks = {
            "pi_uuid": self.pool.uuid,
            "pi_ntargets": node_qty * targets,
            "pi_nnodes": node_qty,
            "pi_ndisabled": 0
        }
        rebuild_checks = {
            "rs_errno": 0,
            "rs_state": 1,
            "rs_obj_nr": 0,
            "rs_rec_nr": 0
        }
        self.assertTrue(
            self.pool.check_pool_info(**info_checks),
            "Invalid pool information detected before rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(**rebuild_checks),
            "Invalid pool rebuild info detected before rebuild")
        self.pool.display_pool_daos_space("after creation")

        # perform ior before rebuild
        self.log.info("..(1)Start IOR before rebuild")
        for ind in range(ior_loop):
            self.log.info("..Starting ior run number %s", ind)
            self.run_ior_with_pool()

        # Kill the server and trigger rebuild
        self.log.info("..(2)Starting rebuild by killing rank %s", rank)
        self.server_managers[0].stop_ranks([rank], self.d_log, force=True)

        # Wait for rebuild to start.
        self.log.info("..(3)Wait for rebuild to start for race condition")
        self.pool.wait_for_rebuild(True, interval=1)

        # Race condition, create containers write and read during rebuild.
        self.log.info("..(4)Create containers, write/read during rebuild")
        self.add_containers_during_rebuild(qty=cont_qty)
        self.access_container()

        # Wait for rebuild to complete.
        self.log.info("..(5)Wait for rebuild to finish")
        self.pool.wait_for_rebuild(False, interval=1)

        self.pool.set_query_data()
        rebuild_status = self.pool.query_data["response"]["rebuild"]["state"]
        self.log.info("Pool %s rebuild status:%s", self.pool.uuid,
                      rebuild_status)

        # Check for pool and rebuild info after rebuild
        self.log.info("..(6)Check for pool and rebuild info after rebuild")
        info_checks["pi_ndisabled"] += targets
        rebuild_checks["rs_obj_nr"] = ">0"
        rebuild_checks["rs_rec_nr"] = ">0"
        rebuild_checks["rs_state"] = 2
        self.assertTrue(
            self.pool.check_pool_info(**info_checks),
            "Invalid pool information detected before rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(**rebuild_checks),
            "Invalid pool rebuild info detected after rebuild")

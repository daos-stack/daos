#!/usr/bin/python3
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from time import sleep, time
from apricot import TestWithServers
from general_utils import get_random_bytes, DaosTestError
from test_utils_container import TestContainerData


class RbldNoCapacity(TestWithServers):
    """Test class for failed pool rebuild.

    Test Class Description:
        This class contains tests for pool rebuild.

    :avocado: recursive
    """

    def test_rebuild_no_capacity(self):
        """Jira ID: DAOS-8846.

        Test Description:
            Create and connect to a pool and container. Full fill the pool
            container, verify the pool information after rebuild, make sure
            correct error of pool full status after rebuild.
            Test steps:
            (1)Check for pool and rebuild info
            (2)Display pool free space before write
            (3)Start write data to full fill the container
            (4)Display pool size after write before rebuild
            (5)Stop rank for rebuild
            (6)Wait for rebuild started
            (7)Poll and verify pool rebuild status with error after rebuild
            (8)Verify pool and rebuild info after rebuild

        Use Cases:
            Full fill pool and verify pool by query after rebuild.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium,ib2
        :avocado: tags=pool,rebuild
        :avocado: tags=no_cap

        """
        # Get the test params
        targets = self.params.get("targets", "/run/server_config/*/0/*")
        rank = self.params.get("rank_to_kill", "/run/rebuild/*")
        engines_per_host = self.params.get("engines_per_host",
                                           "/run/server_config/*")
        pool_query_timeout = self.params.get('pool_query_timeout', "/run/pool/*")
        interval = self.params.get('pool_query_interval', "/run/pool/*")
        test_data_list = self.params.get('test_data_list', "/run/pool/*")
        oclass = self.params.get('oclass', "/run/pool/*")
        err_pool_full = -1007

        # Create a pool and container
        self.prepare_pool()
        self.add_container(self.pool)
        self.container.open()

        # make sure pool looks good before we start
        self.log.info("..(1)Check for pool and rebuild info ")
        pool_checks = {
            "pi_nnodes": len(self.hostlist_servers) * engines_per_host,
            "pi_ntargets": len(self.hostlist_servers) * targets * engines_per_host,
            "pi_ndisabled": 0
        }
        rebuild_checks = {
            "rs_state": 1,
            "rs_obj_nr": 0,
            "rs_rec_nr": 0
        }
        self.assertTrue(
            self.pool.check_pool_info(**pool_checks),
            "#Invalid pool information detected before rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(**rebuild_checks),
            "#Invalid pool rebuild info detected before rebuild")

        # Display pool size before write
        free_space_before = self.pool.get_pool_free_space()
        self.log.info("..(2)Display pool free space before write: %s", free_space_before)

        # Write data to full fill the pool that will not be able to be rebuilt
        self.log.info("..(3)Start write data to full fill the container")
        written_pload = 0
        for payload_size in test_data_list:
            write_count = 0
            while True:
                self.d_log.debug("writing obj {0} sz {1} to "
                                 "container".format(write_count, payload_size))
                my_str = b"A" * payload_size
                dkey = get_random_bytes(5)
                akey = get_random_bytes(5)
                try:
                    written_pload += payload_size
                    self.container.written_data.append(TestContainerData(False))
                    self.container.written_data[-1].write_record(
                        self.container, akey, dkey, my_str, obj_class=oclass)
                    self.d_log.debug("wrote obj {0}, sz {1}".format(write_count,
                                                                    payload_size))
                    write_count += 1
                except DaosTestError as excep:
                    if not str(err_pool_full) in repr(excep):
                        self.log.error("#caught exception while writing "
                                       "object: %s", repr(excep))
                        self.container.close()
                        self.fail("#caught exception while writing "
                                  "object: {}".format(repr(excep)))
                    else:
                        self.log.info("..pool is too full for %s byte "
                                      "objects", payload_size)
                        break

        # Display pool size after write
        free_space_after = self.pool.get_pool_free_space()
        self.log.info("..(4)Pool free space after write: %s", free_space_after)

        # query the pool before rebuild
        self.log.info("....Pool query after filling, written_pload=%s", written_pload)
        self.pool.set_query_data()
        self.log.info(
            "..Pool %s query data: %s\n", self.pool.uuid, self.pool.query_data)

        # Start rebuild
        rank = 1
        self.log.info("..(5)Stop rank for rebuild")
        self.server_managers[0].stop_ranks([rank], self.d_log, force=True)

        # Wait for rebuild started
        self.log.info("..(6)Wait for rebuild started")
        self.pool.wait_for_rebuild(True, interval=1)

        # Verify for pool full error after rebuild
        self.log.info("..(7)Poll and verify pool rebuild status with error")
        status = 0
        retry = 1
        start = time()
        while status != err_pool_full and (time() - start < pool_query_timeout):
            self.pool.set_query_data()
            status = self.pool.query_data["response"]["rebuild"]["status"]
            state = self.pool.query_data["response"]["rebuild"]["state"]
            self.log.info("===>%s, qdata=%s", retry, self.pool.query_data)
            self.log.info("===>%s status=%s, state=%s", retry, status, state)
            sleep(interval)
            retry += 1
        if status != err_pool_full:
            self.fail("#Pool full with rebuild, error -1007 did not show")

        # Check for pool and rebuild info after rebuild
        self.log.info("..(8)Verify pool and rebuild info after rebuild")
        pool_checks["pi_ndisabled"] = ">0"
        rebuild_checks["rs_obj_nr"] = ">=0"
        rebuild_checks["rs_rec_nr"] = ">=0"
        rebuild_checks["rs_state"] = ">=0"
        self.assertTrue(
            self.pool.check_pool_info(**pool_checks),
            "#Invalid pool information detected after rebuild")
        self.assertTrue(
            self.pool.check_rebuild_status(**rebuild_checks),
            "#Invalid pool rebuild info detected after rebuild")
        self.log.info("=Test Passed, expected error -1007 detected after "
                      "rebuild with no pool capacity")

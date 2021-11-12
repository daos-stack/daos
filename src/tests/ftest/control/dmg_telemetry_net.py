#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

    SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from re import search

from mdtest_test_base import MdtestBase
from telemetry_test_base import TestWithTelemetry

# pylint: disable=too-few-public-methods,too-many-ancestors
# pylint: disable=attribute-defined-outside-init
class TestWithTelemetryNet(MdtestBase, TestWithTelemetry):
    """Rebuild test cases featuring mdtest.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    def test_net_telemetry(self):
        """Jira ID: DAOS-9020.

        Test Description: Verify engine net telemetry metrics.

        Use Cases:
          * Create pool and container.
          * Use mdtest to create 120K files with 3-way replication.
          * Stop one server.
          * Ensure engine_net_ofi_sockets_req_timeout is greater than 0.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=control,telemetry,net
        :avocado: tags=test_with_telemetry_net,test_net_telemetry
        """

        all_metrics = self.telemetry.get_all_server_metrics_names(
            self.server_managers[0])
        p = r"engine_net_\w+_req_timeout"
        metrics = list(filter(lambda x: search(p, x), all_metrics))

        # Initialize req_timeouts to zero, as we expect no RPC timeouts at
        # this stage
        req_timeouts = 0

        # set params
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")
        self.dmg = self.get_dmg_command()

        # create pool
        self.add_pool(connect=False)

        # make sure pool looks good before we start
        checks = {
            "pi_nnodes": len(self.hostlist_servers),
            "pi_ntargets": len(self.hostlist_servers) * targets,
            "pi_ndisabled": 0,
        }
        self.assertTrue(
            self.pool.check_pool_info(**checks),
            "Invalid pool information detected before rebuild")

        self.assertTrue(
            self.pool.check_rebuild_status(rs_errno=0, rs_done=1,
                                           rs_obj_nr=0, rs_rec_nr=0),
            "Invalid pool rebuild info detected before rebuild")

        # create 1st container
        self.add_container(self.pool)
        # start 1st mdtest run and let it complete
        self.execute_mdtest()
        # Kill rank[6] and wait for rebuild to complete

        self.server_managers[0].stop_ranks([rank[0]], self.d_log, force=True)

        # Remove the killed host from the clustershell telemetry hostlist
        self.telemetry.hosts.remove(self.hostlist_servers[rank[0]])

        try:
            data = self.telemetry.get_metrics(metrics[0])
        except DaosTestError as error:
            self.log.info("self.telemetry.get_metrics may failed on at least "
                          "one host, but may have succeeded elsewhere.")

        for host, v1 in data.items():
            for metric in data[host][metrics[0]]["metrics"].items():
                req_timeouts += metric["value"]

        if req_timeouts > 0:
            self.log.info("Expected %s values "
                          "to be greater than 0, and it is: %d.",
                          req_timeouts, metrics[0])
        else:
            self.fail("Expected {} to be greater than 0.".format(metrics[0]))

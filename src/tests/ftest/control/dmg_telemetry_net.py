#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

    SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import threading
import time

from re import search
from mdtest_test_base import MdtestBase
from telemetry_test_base import TestWithTelemetry
from general_utils import DaosTestError, pcmd
from command_utils import CommandFailure
from apricot import skipForTicket

# pylint: disable=too-few-public-methods,too-many-ancestors
# pylint: disable=attribute-defined-outside-init
class TestWithTelemetryNet(MdtestBase, TestWithTelemetry):
    """Rebuild test cases featuring mdtest.

    This class contains tests for pool rebuild that feature I/O going on
    during the rebuild using IOR.

    :avocado: recursive
    """

    def pre_tear_down(self):
        """Tear down any test-specific steps prior to running tearDown().

        Returns:
            list: a list of error strings to report after all tear down
            steps have been attempted

        """
        # kill any hanging mdtest processes
        cmd = "pkill -9 mdtest"
        for retry in range(0, 5):
            ret_codes = pcmd(self.hostlist_servers, cmd)
            if len(ret_codes) > 1 or 0 not in ret_codes:
                self.log.info("Retry #%d: Not all '%s' commands in pcmd "
                              "returned 0.", retry, cmd)

        # Ignore errors cleaning up mdtest
        return []

    def tearDown(self):
        try:
            super().tearDown()
        except (CommandFailure, DaosTestError) as excep:
            self.log.info("%s: tearDown threw an exception "
                          "because a rank was (intentionally) "
                          "stopped midway through a run of mdtest.",
                          repr(excep))
            pass

    @skipForTicket("DAOS-9106")
    def test_net_telemetry(self):
        """Jira ID: DAOS-9020.

        Test Description: Verify engine net telemetry metrics.

        Use Cases:
          * Create pool and container.
          * Use mdtest to create 120K files with 3-way replication.
          * Stop one server.
          * Ensure engine_net_ofi_sockets_req_timeout is greater than 0.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=control,telemetry,net
        :avocado: tags=test_with_telemetry_net,test_net_telemetry
        """

        # Get the req_timeout metric name, which depends on the libfabric
        # provider name
        all_metrics = self.telemetry.get_all_server_metrics_names(
            self.server_managers[0])
        p = r"engine_net_\w+_req_timeout"
        metrics = list(filter(lambda x: search(p, x), all_metrics))

        # Initialize req_timeouts based on what's already populated in the
        # telemtry system, which may or may not be zero.
        req_timeouts_start = 0

        data = {}
        try:
            data = self.telemetry.get_metrics(metrics[0])
        except (CommandFailure, DaosTestError) as excep:
            self.log.info("self.telemetry.get_metrics failed on at least one "
                          "host with '{}', but may have succeeded elsewhere.",
                          repr(excep))
            pass

        for host, value in data.items():
            for metric in value[metrics[0]]["metrics"]:
                req_timeouts_start += metric["value"]

        req_timeouts_finish = req_timeouts_start

        # set params
        targets = self.params.get("targets", "/run/server_config/*")
        rank = self.params.get("rank_to_kill", "/run/testparams/*")

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

        # create 1st container
        self.add_container(self.pool)

        # start mdtest run
        thread = threading.Thread(target=self.execute_mdtest)
        thread.start()
        time.sleep(5)

        self.server_managers[0].stop_ranks([rank[0]], self.d_log, force=True)
        time.sleep(10)

        # _req_timeout metric may not be on all hosts
        self.telemetry.hosts.clear()
        self.telemetry.hosts.add(self.hostlist_servers[0])

        data = {}
        try:
            data = self.telemetry.get_metrics(metrics[0])
        except (CommandFailure, DaosTestError) as excep:
            self.log.info("self.telemetry.get_metrics failed on at least one "
                          "host with %s, but may have succeeded elsewhere.",
                          repr(excep))
            pass

        for host, value in data.items():
            for metric in value[metrics[0]]["metrics"]:
                req_timeouts_finish += metric["value"]

        req_timeouts_delta = req_timeouts_finish - req_timeouts_start
        if req_timeouts_delta > 0:
            self.log.info("Expected %s values to increase "
                          "during this test, and they did: %s.",
                          str(metrics[0]), str(req_timeouts_delta))
        else:
            self.fail("Expected %s to increase during this "
                      "test.", str(metrics[0]))

        # wait a bit for mdtest to complete
        try:
            thread.join(timeout=5)
        except Exception as excep:
            self.log.info("MDtest threw an exception (%s), but this is not "
                          "entirely unexpected, since we killed one of it's "
                          "ranks.", repr(excep))
            pass

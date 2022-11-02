#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from avocado.core.exceptions import TestFail

from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics
from general_utils import percent_change


class IorPerRank(IorTestBase):
    # pylint: disable=attribute-defined-outside-init
    # pylint: disable=too-few-public-methods
    # pylint: disable=too-many-ancestors
    """Test Class Description: Test class to run ior per rank basis and
                               collect read/write data to identify bad
                               nodes.
    :avocado: recursive
    """

    def execute_ior_thread(self, rank):
        """
        Method to execute ior for different transfer sizes,
        collect the performance numbers and match with the
        expected values.
        Args:
            rank(str): Daos server rank on which ior needs to be run.
        """

        self.log.info("Running Test on rank: %s", rank)
        # create the pool on specified rank.
        self.create_pool([rank])
        # execute ior on given rank for different transfer sizes and collect the results
        for idx, _ in enumerate(self.transfer_sizes):
            self.ior_cmd.transfer_size.update(self.transfer_sizes[idx])
            dfs_out = self.run_ior_with_pool(fail_on_warning=self.log.info)
            dfs_perf = IorCommand.get_ior_metrics(dfs_out)

            # Destroy container, to be sure we use newly created container in next iteration
            self.container.destroy()
            self.container = None

            # gather actual and expected perf data to be compared
            if idx == 0:
                dfs_max_write = float(dfs_perf[0][IorMetrics.Max_MiB])
                dfs_max_read = float(dfs_perf[1][IorMetrics.Max_MiB])
                actual_write_x = percent_change(dfs_max_write, self.expected_bw)
                actual_read_x = percent_change(dfs_max_read, self.expected_bw)
            else:
                dfs_max_write = float(dfs_perf[0][IorMetrics.Max_OPs])
                dfs_max_read = float(dfs_perf[1][IorMetrics.Max_OPs])
                actual_write_x = percent_change(dfs_max_write, self.expected_iops)
                actual_read_x = percent_change(dfs_max_read, self.expected_iops)

            # compare actual and expected perf data
            try:
                self.assertLessEqual(abs(actual_write_x), self.write_x,
                                     "Max Write Diff too large for rank: {}".format(rank))
                self.assertLessEqual(abs(actual_read_x), self.read_x,
                                     "Max Read Diff too large for rank: {}".format(rank))
                # collect list of good nodes
                good_node = self.server_managers[0].get_host(rank)
                if good_node not in self.good_nodes:
                    self.good_nodes.append(good_node)
            except TestFail as _error:
                # collect bad nodes
                failed_node = self.server_managers[0].get_host(rank)
                if failed_node not in self.failed_nodes:
                    self.failed_nodes[failed_node] = [rank]
                else:
                    self.failed_nodes[failed_node].append(rank)
                if failed_node in self.good_nodes:
                    self.good_nodes.remove(failed_node)
        # Destroy pool, to be sure we use newly created pool in next iteration
        self.pool.destroy()
        self.pool = None

    def test_iorperrank(self):
        """
        Test Description: Test to check node health using ior with daos.
                          Start entire daos system on each rack of a group.
                          Create pool for each rank.
                          Run ior using 4 good clients over each pool.
                              Large transfer size: 1M
                              Small transfer size: 256B
                          Compare results and isolate bad nodes.
        :avocado: tags=all,deployment,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=ior
        :avocado: tags=iorperrank
        """

        # test params
        self.failed_nodes = {}
        self.good_nodes = []
        self.transfer_sizes = self.params.get("transfer_sizes", self.ior_cmd.namespace)

        # Write/Read performance thresholds and expectations
        self.write_x = self.params.get("write_x", self.ior_cmd.namespace, None)
        self.read_x = self.params.get("read_x", self.ior_cmd.namespace, None)
        self.expected_bw = self.params.get("expected_bw", self.ior_cmd.namespace, None)
        self.expected_iops = self.params.get("expected_iops", self.ior_cmd.namespace, None)

        if (self.write_x is None or self.read_x is None or self.expected_bw is None or
                self.expected_iops is None):
            self.fail("Failed to get write_x, read_x, expected_bw, expected_iops from config")

        # create a list of all the ranks
        rank_list = self.server_managers[0].get_host_ranks(self.hostlist_servers)
        self.log.info("rank_list: %s", rank_list)

        # run ior over DFS
        self.ior_cmd.api.update("DFS")
        for rank in rank_list:
            self.execute_ior_thread(rank)

        # list of good nodes
        if self.good_nodes:
            self.log.info("List of good nodes: %s", self.good_nodes)

        # list the failed node and the rank number associated with that node
        if self.failed_nodes:
            self.log.info("List of failed ranks with corresponding nodes")
            for node, rank in self.failed_nodes.items():
                self.log.info("Node: %s, Rank: %s", node, rank)
            self.fail()

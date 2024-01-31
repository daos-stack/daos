"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from avocado.core.exceptions import TestFail
from ClusterShell.NodeSet import NodeSet
from general_utils import DaosTestError, percent_change
from ior_test_base import IorTestBase
from ior_utils import IorCommand, IorMetrics


class IorPerRank(IorTestBase):
    # pylint: disable=attribute-defined-outside-init
    # pylint: disable=too-few-public-methods
    # pylint: disable=too-many-ancestors
    """Test Class Description: Test class to run ior per rank basis and
                               collect read/write data to identify bad
                               nodes.
    :avocado: recursive
    """

    def execute_ior_per_rank(self, rank):
        """
        Method to execute ior for different transfer sizes,
        collect the performance numbers and match with the
        expected values.
        Args:
            rank(str): Daos server rank on which ior needs to be run.
        """

        self.log.info("Running Test on rank: %s", rank)
        # create the pool on specified rank.
        self.add_pool(connect=False, target_list=[rank])
        self.container = self.get_container(self.pool)

        host = self.server_managers[0].get_host(rank)

        # execute ior on given rank and collect the results
        try:
            self.ior_cmd.flags.update(self.write_flags)
            dfs_out = self.run_ior_with_pool(create_cont=False, fail_on_warning=self.log.info)
            dfs_perf_write = IorCommand.get_ior_metrics(dfs_out)
            self.ior_cmd.flags.update(self.read_flags)
            dfs_out = self.run_ior_with_pool(create_cont=False, fail_on_warning=self.log.info)
            dfs_perf_read = IorCommand.get_ior_metrics(dfs_out)

            # Destroy container, to be sure we use newly created container in next iteration
            self.container.destroy()
            self.container = None

            # gather actual and expected perf data to be compared
            dfs_max_write = float(dfs_perf_write[0][IorMetrics.MAX_MIB])
            dfs_max_read = float(dfs_perf_read[0][IorMetrics.MAX_MIB])
            actual_write_x = abs(percent_change(self.expected_bw, dfs_max_write))
            actual_read_x = abs(percent_change(self.expected_bw, dfs_max_read))

            # verify write performance
            if actual_write_x > self.write_x:
                if host not in self.failed_nodes:
                    self.failed_nodes[host] = []
                self.failed_nodes[host].append(
                    f"rank {rank} low write perf. "
                    f"BW: {dfs_max_write:.2f}/{self.expected_bw:.2f}; "
                    f"percent diff: {actual_write_x:.2f}/{self.write_x:.2f}")

            # verify read performance
            if actual_read_x > self.read_x:
                if host not in self.failed_nodes:
                    self.failed_nodes[host] = []
                self.failed_nodes[host].append(
                    f"rank {rank} low read perf. "
                    f"BW: {dfs_max_read:.2f}/{self.expected_bw:.2f}; "
                    f"percent diff: {actual_read_x:.2f}/{self.read_x:.2f}")

        except (TestFail, DaosTestError) as error:
            if host not in self.failed_nodes:
                self.failed_nodes[host] = []
            self.failed_nodes[host].append(str(error))

        # Destroy pool, to be sure we use newly created pool in next iteration
        self.pool.destroy()
        self.pool = None

    def test_ior_per_rank(self):
        """
        Test Description: Test to check node health using ior with daos.
                          Start entire daos system on each rack of a group.
                          Create pool for each rank.
                          Run ior using 4 good clients over each pool.
                              Large transfer size: 1M
                              Small transfer size: 256B
                          Compare results and isolate bad nodes.
        :avocado: tags=manual
        :avocado: tags=hw,medium
        :avocado: tags=ior,deployment
        :avocado: tags=IorPerRank,test_ior_per_rank
        """

        # test params
        self.failed_nodes = {}
        self.write_flags = self.params.get("write_flags", self.ior_cmd.namespace)
        self.read_flags = self.params.get("read_flags", self.ior_cmd.namespace)

        # Write/Read performance thresholds and expectations
        self.write_x = self.params.get("write_x", self.ior_cmd.namespace, None)
        self.read_x = self.params.get("read_x", self.ior_cmd.namespace, None)
        self.expected_bw = self.params.get("expected_bw", self.ior_cmd.namespace, None)
        self.expected_iops = self.params.get("expected_iops", self.ior_cmd.namespace, None)

        if not all((self.write_x, self.read_x, self.expected_bw, self.expected_iops)):
            self.fail("Failed to get write_x, read_x, expected_bw, expected_iops from config")

        # create a list of all the ranks
        rank_list = self.server_managers[0].get_host_ranks(self.hostlist_servers)
        self.log.info("rank_list: %s", rank_list)

        # run ior over DFS
        for rank in rank_list:
            self.execute_ior_per_rank(rank)

        # the good nodes are any that did not fail
        good_nodes = self.hostlist_servers - NodeSet.fromlist(self.failed_nodes.keys())
        if good_nodes:
            self.log.info("Good nodes: %s", good_nodes)

        # list the failed node and the rank number associated with that node
        if self.failed_nodes:
            self.log.info("List of failed nodes with corresponding ranks")
            for node, reason_list in self.failed_nodes.items():
                for reason in reason_list:
                    self.log.info("%s: %s", node, reason)
            self.fail("Performance check failed for one or more nodes")

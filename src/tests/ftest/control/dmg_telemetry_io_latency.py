#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import re
from avocado.core.exceptions import TestFail
from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry
from telemetry_utils import TelemetryUtils
from ior_utils import IorCommand, IorMetrics
from test_utils_container import TestContainer


class TestWithTelemetryIODtx(IorTestBase, TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Test telemetry engine io basic metrics.

    :avocado: recursive
    """

    def add_containers(self, pool, oclass=None, path="/run/container/*"):
        """Create a list of containers that the various jobs use for storage.

        Args:
            pool: pool to create container
            oclass: object class of container


        """
        rf = None
        # Create a container and add it to the overall list of containers
        self.container.append(
            TestContainer(pool, daos_command=self.get_daos_command()))
        self.container[-1].namespace = path
        self.container[-1].get_params(self)
        # include rf based on the class
        if oclass:
            self.container[-1].oclass.update(oclass)
            redundancy_factor = self.get_rf(oclass)
            rf = 'rf:{}'.format(str(redundancy_factor))
        properties = self.container[-1].properties.value
        cont_properties = (",").join(filter(None, [properties, rf]))
        if cont_properties is not None:
            self.container[-1].properties.update(cont_properties)
        self.container[-1].create()

    def get_rf(self, oclass):
        """Return redundancy factor based on the oclass.

        Args:
            oclass(string): object class.

        return:
            redundancy factor(int) from object type
        """
        rf = 0
        if "EC" in oclass:
            tmp = re.findall(r'\d+', oclass)
            if tmp:
                rf = int(tmp[1])
        elif "RP" in oclass:
            tmp = re.findall(r'\d+', oclass)
            if tmp:
                rf = int(tmp[0]) - 1
        else:
            rf = 0
        return rf

    def convert_to_number(self, size):
        """Convert string to int.

        Args:
            size (str): String from yaml that represents a string with suffix
                        denoting how many bytes. suffix must be B, K, M, G or T
        Returns:
            num: (int) converted string number in bytes

        """
        num = 0
        SIZE_DICT = {"B": 1,
                     "K": 1024,
                     "M": 1024 * 1024,
                     "G": 1024 * 1024 * 1024,
                     "T": 1024 * 1024 * 1024 * 1024}
        # Convert string to bytes
        suffix = size[-1]
        for key in SIZE_DICT:
            if suffix == key:
                num = int(SIZE_DICT[key]) * int(size[:-1])
        self.log.info("%s is converted to %s", size, num)
        return int(num)

    def verify_latency_metrics(self, metrics, ior_metrics, key):
        """Verify latency metric against ior metric.

        Args:
            metrics (dict): dictionary of io latency metrics
            ior_metrics (dict): dictionary of ior metrics
            key (str): common key for dictionary (operation-transfer_size)
        Returns:
            status: (bool) True if metric is verified
        """
        transfer_size = key.split("-")[-1]
        transfers = self.convert_to_number(transfer_size)
        self.log.info("transfer size %s", transfers)
        return True

    def test_io_latency_telmetry_metrics(self):
        """JIRA ID: DAOS-8624.

            Create files of 500M and 1M with transfer size 1M to verify the
            DAOS engine IO DTX telemetry metrics infrastructure.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,telemetry
        :avocado: tags=test_with_telemetry_basic,test_io_telemetry
        :avocado: tags=test_io_latency_telemetry

        """
        block_size = self.params.get("block_size", "/run/*")
        transfer_sizes = self.params.get("transfer_sizes", "/run/*")
        self.container = []
        key_list = []
        verification_results = []
        metrics_data = {}
        ior_metrics = {}
        for transfer_size in transfer_sizes:
            self.add_pool(connect=False)
            oclass = self.ior_cmd.dfs_oclass.value
            self.add_containers(self.pool, oclass)
            for operation in ["update", "fetch"]:
                flags = self.params.get("F", "/run/ior/ior{}flags/".format(
                    operation))
                key = "{}-{}".format(operation, transfer_size)
                key_list.append(key)
                self.log.info(
                    "<<< Start ior %s with Block Size=%s, transfer_size=%s",
                    operation, block_size, transfer_size)
                self.ior_cmd.block_size.update(block_size)
                self.ior_cmd.transfer_size.update(transfer_size)
                self.ior_cmd.flags.update(flags)
                self.ior_cmd.set_daos_params(
                    self.server_group, self.pool, self.container[-1].uuid)
                # Run ior command
                ior_results = self.run_ior_with_pool(
                        timeout=200, create_pool=False, create_cont=False)
                ior_metrics[key] = IorCommand.get_ior_metrics(ior_results)
                if operation == "update":
                    metrics_data[key] = self.telemetry.get_io_metrics(
                        ["engine_io_latency_update"])
                else:
                    metrics_data[key] = self.telemetry.get_io_metrics(
                        ["engine_io_latency_fetch"])
                        # Destroy the container and the pool.
            self.destroy_containers(containers=self.container[-1])
            self.destroy_pools(pools=self.pool)
        # check dmg latency metrics against ior latency metrics
        for key in key_list:
            if self.verify_latency_metrics(metrics_data[key], ior_metrics[key], key):
                verification_results.append(["PASSED", key])
            else:
                verification_results.append(["FAILED", key])
        self.log.error("Summary of IOR small test results:")
        errors = False
        for item in verification_results:
            self.log.info("  %s  %s", item[0], item[1])
            if item[0] == "FAILED":
                errors = True
        if errors:
            self.fail("Test FAILED")

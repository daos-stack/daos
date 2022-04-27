#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail

from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry
from telemetry_utils import TelemetryUtils


class TestWithTelemetryIOBasic(IorTestBase, TestWithTelemetry):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-many-nested-blocks
    """Test telemetry engine io basic metrics.

    :avocado: recursive
    """

    def verify_io_test_metrics(self, io_test_metrics, metrics_data, threshold):
        """ Verify telemetry io metrics from metrics_data.

        Args:
            io_test_metrics (list): list of telemetry io metrics.
            metrics_data (dict): a dictionary of host keys linked to a
                                 list of io metric names.
            threshold (int): test io metrics threshold.

        """
        status = True
        for name in sorted(io_test_metrics):
            self.log.info("  --telemetry metric: %s", name)
            self.log.info(
                "    %-9s %-12s %-4s %-6s %-6s %s",
                "TestLoop", "Host", "Rank", "Target", "Size", "Value")
            for key in sorted(metrics_data):
                m_data = metrics_data[key]
                if key == 0:
                    testloop = "Initial"
                else:
                    testloop = str(key)
                for host in sorted(m_data[name]):
                    for rank in sorted(m_data[name][host]):
                        for target in sorted(m_data[name][host][rank]):
                            for size in sorted(m_data[name][host][rank][target]):
                                value = m_data[name][host][rank][target][size]
                                invalid = ""
                                # Verify value within range
                                if (value < threshold[0] or value >= threshold[1]):
                                    status = False
                                    invalid = "*out of valid range"
                                # Verify if min < max
                                if "_min" in name:
                                    name2 = name.replace("_min", "_max")
                                    if value > m_data[name2][host][rank][target][size]:
                                        status = False
                                        invalid += " *_min > _max"
                                # Verify if value decremental
                                if ("_min" in name or "_max" in name) and key > 0:
                                    if value < metrics_data[key-1][name][host][rank][target][size]:
                                        status = False
                                        invalid += " *value decreased"
                                self.log.info(
                                    "    %-9s %-12s %-4s %-6s %-6s %s %s",
                                    testloop, host, rank, target, size, value,
                                    invalid)
        if not status:
            self.fail("##Telemetry test io metrics verification failed.")

    def display_io_test_metrics(self, metrics_data):
        """ Display metrics_data.

        Args:
            metrics_data (dict): a dictionary of host keys linked to a
                                 list of io metric names.
        """
        for key in sorted(metrics_data):
            self.log.info(
                "\n  %12s: %s",
                "Initial " if key == 0 else "Test Loop {}".format(key),
                metrics_data[key])

    def test_io_telmetry_metrics_basic(self):
        """JIRA ID: DAOS-5241

            Create files of 500M and 1M with transfer size 1M to verify the
            DAOS engine IO telemetry basic metrics infrastructure.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=control,telemetry
        :avocado: tags=test_with_telemetry_basic,test_io_telemetry
        :avocado: tags=test_io_telemetry_basic

        """
        block_sizes = self.params.get("block_sizes", "/run/*")
        transfer_sizes = self.params.get("transfer_sizes", "/run/*")
        threshold = self.params.get("io_test_metrics_valid", "/run/*")
        test_metrics = TelemetryUtils.ENGINE_IO_DTX_COMMITTED_METRICS +\
            TelemetryUtils.ENGINE_IO_OPS_FETCH_ACTIVE_METRICS +\
            TelemetryUtils.ENGINE_IO_OPS_UPDATE_ACTIVE_METRICS
        i = 0
        self.add_pool(connect=False)
        self.add_container(pool=self.pool)
        metrics_data = {}
        for block_size in block_sizes:
            for transfer_size in transfer_sizes:
                metrics_data[i] = self.telemetry.get_io_metrics(test_metrics)
                i += 1
                self.log.info("==Start ior testloop: %s, Block Size = %s, "
                              "transfer_size =  %s", i, block_size,
                              transfer_size)
                self.ior_cmd.block_size.update(block_size)
                self.ior_cmd.transfer_size.update(transfer_size)
                test_file_suffix = "_{}".format(i)
                # Run ior command.
                try:
                    self.run_ior_with_pool(
                        timeout=200, create_pool=False, create_cont=False,
                        test_file_suffix=test_file_suffix)
                except TestFail:
                    self.log.info("#ior command failed!")
        metrics_data[i] = self.telemetry.get_io_metrics(test_metrics)
        self.display_io_test_metrics(metrics_data)
        self.verify_io_test_metrics(test_metrics, metrics_data, threshold)
        self.log.info("------Test passed------")

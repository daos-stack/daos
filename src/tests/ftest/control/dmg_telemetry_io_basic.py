#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from avocado.core.exceptions import TestFail

from ior_test_base import IorTestBase
from telemetry_test_base import TestWithTelemetry


class TestWithTelemetryIOBasic(IorTestBase,TestWithTelemetry):
    # pylint: disable=too-many-ancestors
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
        for host in self.hostlist_servers:
            self.log.info("==Host: %s", host)
            status = 0
            for name in io_test_metrics:
                self.log.info("  --telemetry metric: %s", name)
                for key in sorted(metrics_data):
                    m_data = metrics_data[key]
                    self.log.info(
                        "  %12s: %s",
                        "Initial    " if key == 0 else "Test Loop {}".format(
                            key), metrics_data[key][host][name])

                    #Detail for each test io metrics threshold to be updated
                    if m_data[host][name] < threshold:
                        self.log.info(
                            "#Host %s, metrics %s, value %s "
                            "less than the threshold limit %s",
                            host, name, m_data[host][name], threshold)
                        status = 1
            if status == 1:
                self.fail("##Telemetry test io metrics verification failed.")

    def display_io_test_metrics(self, metrics_data):
        """ Display metrics_data.

        Args:
            metrics_data (dict): a dictionary of host keys linked to a
                                 list of io metric names.
        """
        for key in sorted(metrics_data):
            self.log.info(
                "  %12s: %s",
                "Initial " if key == 0 else "Test Loop {}".format(key),
                metrics_data[key])


    def test_telmetry_metrics(self):
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
        test_metrics = self.params.get("io_test_metrics", "/run/*")
        threshold = self.params.get("io_test_metrics_threshold", "/run/*")
        i = 0
        self.add_pool(connect=False)
        self.add_container(pool=self.pool)
        metrics_data = {}
        for block_size in block_sizes:
            for transfer_size in transfer_sizes:
                metrics_data[i] = self.telemetry.get_io_metrics()
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
        metrics_data[i] = self.telemetry.get_io_metrics()
        self.display_io_test_metrics(metrics_data)
        self.verify_io_test_metrics(test_metrics, metrics_data, threshold)
        self.log.info("------Test passed------")

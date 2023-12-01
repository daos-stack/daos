"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ior_test_base import IorTestBase
from ior_utils import IorMetrics


class TestWithScrubberPerf(IorTestBase):
    # pylint: disable=too-many-nested-blocks
    """Basic Scrubber Test

    :avocado: recursive
    """

    def test_scrubber_performance(self):
        """JIRA ID: DAOS-7372
        Check IOR performance with checksum enabled.

        :avocado: tags=all,manual
        :avocado: tags=hw,medium
        :avocado: tags=scrubber
        :avocado: tags=test_scrubber_performance
        """
        write_flags = self.params.get("write_flags", self.ior_cmd.namespace)
        read_flags = self.params.get("read_flags", self.ior_cmd.namespace)
        self.add_pool()
        self.add_container(self.pool)
        # First get the write metrics
        self.ior_cmd.flags.update(write_flags)
        cmd_result = self.run_ior_with_pool(create_cont=False,
                                            fail_on_warning=self.log.info)
        metrics = self.ior_cmd.get_ior_metrics(cmd_result)
        ior_write_size_without_scrubber = float(metrics[0][IorMetrics.MAX_MIB])
        # Now, get the read metrics
        self.ior_cmd.flags.update(read_flags)
        cmd_result = self.run_ior_with_pool(create_cont=False,
                                            fail_on_warning=self.log.info)
        metrics = self.ior_cmd.get_ior_metrics(cmd_result)
        ior_read_size_without_scrubber = float(metrics[0][IorMetrics.MAX_MIB])
        self.log.info("IOR Metrics without scrubber enabled")
        self.log.info("====================================")
        self.log.info("ior_write_size = %d", ior_write_size_without_scrubber)
        self.log.info("ior_read_size = %d", ior_read_size_without_scrubber)
        self.pool.set_prop(properties="scrub:timed,scrub-freq:1")
        # Wait for the scrubber to scan objects.
        time.sleep(15)
        # Get the write metrics with scrubber enabled.
        self.ior_cmd.flags.update(write_flags)
        cmd_result = self.run_ior_with_pool(create_cont=False,
                                            fail_on_warning=self.log.info)
        metrics = self.ior_cmd.get_ior_metrics(cmd_result)
        ior_write_size_with_scrubber = float(metrics[0][IorMetrics.MAX_MIB])
        # Now, get the read metrics with scrubber enabled.
        self.ior_cmd.flags.update(read_flags)
        cmd_result = self.run_ior_with_pool(create_cont=False,
                                            fail_on_warning=self.log.info)
        metrics = self.ior_cmd.get_ior_metrics(cmd_result)
        ior_read_size_with_scrubber = float(metrics[0][IorMetrics.MAX_MIB])
        self.log.info("IOR Metrics with scrubber enabled")
        self.log.info("=================================")
        self.log.info("ior_write_size = %d", ior_write_size_with_scrubber)
        self.log.info("ior_read_size = %d", ior_read_size_with_scrubber)
        self.assertLessEqual(ior_write_size_with_scrubber,
                             ior_write_size_without_scrubber,
                             "Max Write Diff too large")
        self.assertLessEqual(ior_read_size_with_scrubber,
                             ior_read_size_without_scrubber,
                             "Max Read Diff too large")

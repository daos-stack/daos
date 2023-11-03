"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from ior_test_base import IorTestBase


class TestWithScrubberPerf(IorTestBase):
    # pylint: disable=too-many-nested-blocks
    """Basic Scrubber Test

    :avocado: recursive
    """

    def test_scrubber_performance(self):
        """JIRA ID: DAOS-7372
        Check IOR performance with checksum enabled.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber
        :avocado: tags=test_scrubber_performance
        """
        self.add_pool()
        self.get_container(self.pool)
        values = "Pool : {} Container: {}".format(self.pool, self.container)
        self.log.info(values)
        cmd_result = self.run_ior_with_pool(create_pool=False, create_cont=False)
        metrics = self.ior_cmd.get_ior_metrics(cmd_result)
        ior_write_size_without_scrubber = int(metrics[0][22])
        self.log.info("IOR Metrics without scrubber enabled")
        self.log.info("====================================")                     )
        self.log.info("IOR metrics = %s", metrics)
        self.log.info("ior_write_size = %d", ior_write_size_without_scrubber)
        self.pool.set_property("scrub", "timed")
        self.pool.set_property("scrub-freq", "1")
        time.sleep(15)
        cmd_result = self.run_ior_with_pool()
        metrics = self.ior_cmd.get_ior_metrics(cmd_result)
        ior_write_size_with_scrubber = int(metrics[0][22])
        self.log.info("IOR Metrics with scrubber enabled")
        self.log.info("=================================")                     )
        self.log.info("IOR metrics = %s", metrics)
        self.log.info("ior_write_size = %d", ior_write_size_with_scrubber)

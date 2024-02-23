"""
  (C) Copyright 2018-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from datetime import datetime, timedelta

from general_utils import get_journalctl, journalctl_time
from scrubber_test_base import TestWithScrubber


class TestWithScrubberTargetEviction(TestWithScrubber):
    # pylint: disable=too-many-nested-blocks
    """Inject Checksum Fault with scrubber enabled
    and scrubber threshold set to a certain value.

    :avocado: recursive
    """
    def test_scrubber_ssd_auto_eviction(self):
        """JIRA ID: DAOS-7300

        1. Create checksum faults above scrubber threshold
        and see whether SSD auto eviction works as expected.
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber,faults
        :avocado: tags=TestWithScrubberTargetEviction,test_scrubber_ssd_auto_eviction
        """
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        initial_metrics = {}
        final_metrics = {}
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        self.dmg_cmd.pool_query(self.pool.identifier)
        initial_metrics = self.scrubber.get_scrub_corrupt_metrics()
        t_start = journalctl_time()
        t_end = journalctl_time(datetime.now() + timedelta(seconds=60))
        self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        # Wait for a minute for the scrubber to take action and evict target
        # after corruption threshold reached.
        self.log.info("Sleeping for 60 seconds")
        time.sleep(60)
        # Check the journalctl for data corrupt message.
        results = get_journalctl(hosts=self.hostlist_servers, since=t_start,
                                 until=t_end, journalctl_type="daos_server")
        self.log.info(results)
        str_to_match = "Data corruption detected"
        occurrence = 0
        for count, _ in enumerate(self.hostlist_servers):
            occurrence = results[count]["data"].count(str_to_match)
        if occurrence > 0:
            self.log.info("Data corrupted occurrence %s", occurrence)
        else:
            self.log.info("RAS data corrupted messages missing on system logs")
            self.fail("------Test Failed------")
        self.dmg_cmd.pool_query(self.pool.identifier)
        final_metrics = self.scrubber.get_scrub_corrupt_metrics()
        status = self.verify_scrubber_metrics_value(initial_metrics, final_metrics)
        if status is False:
            self.log.info("------Test Failed-----")
            self.log.info("---No metrics value change----")
            self.fail("------Test Failed-----")
        self.log.info("------Test passed------")

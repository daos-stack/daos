"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from time import sleep

from scrubber_test_base import TestWithScrubber


class TestWithScrubberFreq(TestWithScrubber):
    # pylint: disable=too-many-nested-blocks
    """Test the scrubber frequency option.

    :avocado: recursive
    """
    def test_objects_scrubbed_properly(self):
        """JIRA ID: DAOS-7371
            Test scrubber frequency can be dynamically updated.

            1. Create a test pool without scrubber properties.
               The default scrub frequency is set to 1 week.
            2. Initial scrub happens when the objects are created.
            3. Check objects are not scrubbed after initial scan.
               Wait for 5 minutes and see scrubber not doing
               any more object scrubbing now.
            4. Update the scrubber frequency to 5 seconds.
            5. Observe the objects are scrubbed every 5 seconds.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=scrubber
        :avocado: tags=TestWithScrubberFreq,test_objects_scrubbed_properly
        """
        pool_prop = self.params.get("properties", '/run/pool/*')
        cont_prop = self.params.get("properties", '/run/container/*')
        self.create_pool_cont_with_scrubber(pool_prop=pool_prop, cont_prop=cont_prop)
        # Run IOR and gather the total scrubbed metrics information.
        self.run_ior_and_check_scruber_status(pool=self.pool, cont=self.container)
        # Wait for 5 minutes to get first scrubber bytes scrubbed metrics.
        # NOTE: This value could change depending on the IOR data (objects created)
        self.log.info("Sleeping for 5 minutes pool property set to scrub:timed")
        sleep(300)
        self.log.info("Initial scrubber metrics with scrub:timed")
        initial_scrubbed_metrics = self.scrubber.get_scrubber_bytes_scrubbed_total()
        self.log.info("Sleeping another 5 minutes pool property set to scrub:timed")
        sleep(300)
        self.log.info("Final scrubber metrics with scrub:timed")
        final_scrubbed_metrics = self.scrubber.get_scrubber_bytes_scrubbed_total()
        status = self.verify_scrubber_metrics_value(initial_scrubbed_metrics,
                                                    final_scrubbed_metrics)
        if status is True:
            self.fail("--Test Failed: Metrics Value is Changing--")
        # Now set the scrub_freq to 5 seconds.
        self.pool.set_property("scrub_freq", "5")
        self.log.info("Sleeping for 5 secs pool property scrub:timed,scrub_freq:5")
        sleep(5)
        self.log.info("Initial scrubber metrics with scrub:timed,scrub_freq:5")
        initial_scrubbed_metrics = self.scrubber.get_scrubber_bytes_scrubbed_total()
        # Now wait for 60 seconds
        self.log.info("Sleeping for 60 secs pool property scrub:timed,scrub_freq:5")
        sleep(60)
        self.log.info("Final scrubber metrics with scrub:timed,scrub_freq:5")
        final_scrubbed_metrics = self.scrubber.get_scrubber_bytes_scrubbed_total()
        status = self.verify_scrubber_metrics_value(initial_scrubbed_metrics,
                                                    final_scrubbed_metrics)
        if status is False:
            self.fail("--Test Failed: No metrics value change--")
        self.log.info("Scrubber is scanning the objects at proper intervals")
        self.log.info("Test Passed")

#!/usr/bin/python
"""
(C) Copyright 2021-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from telemetry_utils import TelemetryUtils


class ScrubberUtils(TelemetryUtils):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Class for Scrubber Utils."""

    def is_scrubber_started(self):
        """This method is used to check whether scrubber is
          started.

        Returns:
            info (dict): Scrubber started output.
        """
        TEST_METRICS = "engine_pool_scrubber_scrubber_started"
        info = self.get_metrics(TEST_METRICS)
        return info

    def get_scrub_corrupt_metrics(self):
        """This method is used to get scrubber corrupt metrics.

        Returns:
            info (dict): scrubber corrupt metrics.
        """
        CORRUPT_METRICS = "engine_pool_scrubber_corruption_total"
        info = self.get_metrics(CORRUPT_METRICS)
        return info

    def get_csum_total_metrics(self):
        """This method is used to get scrubber csum total
        metrics.

        Returns:
            info (dict): scrubber csum total metrics.
        """
        CSUM_METRICS = "engine_pool_scrubber_csums_total"
        info = self.get_metrics(CSUM_METRICS)
        return info

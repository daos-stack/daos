"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from telemetry_utils import TelemetryUtils


class ScrubberUtils(TelemetryUtils):
    """Class for Scrubber Utils."""

    def is_scrubber_started(self):
        """This method is used to check whether scrubber is started.

        Returns:
            dict: Scrubber started output.
        """
        return self.get_metrics("engine_pool_scrubber_scrubber_started")

    def get_scrub_corrupt_metrics(self):
        """This method is used to get scrubber corrupt metrics.

        Returns:
            dict: scrubber corrupt metrics.
        """
        return self.get_metrics("engine_pool_scrubber_corruption_total")

    def get_csum_total_metrics(self):
        """This method is used to get scrubber csum total metrics.

        Returns:
            dict: scrubber csum total metrics.
        """
        return self.get_metrics("engine_pool_scrubber_csums_total")

    def get_scrubber_bytes_scrubbed_total(self):
        """This method is used to get scrubber bytes scrubbed total
        metrics information.

        Returns:
            dict: scrubber bytes scrubbed total.
        """
        return self.get_metrics("engine_pool_scrubber_bytes_scrubbed_total")

#!/usr/bin/python
"""
(C) Copyright 2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from logging import getLogger
from ClusterShell.NodeSet import NodeSet
from telemetry_utils import TelemetryUtils


class ScrubberUtils(TelemetryUtils):
    # pylint: disable=too-few-public-methods, too-many-ancestors
    """Class for Scrubber Utils."""

    def setUp(self):
        """Set up for test case."""
        super().setUp()

    def is_scrubber_started(self):
        """This method is used to check whether scrubber is
          started.
        """
        TEST_METRICS = "engine_pool_scrubber_scrubber_started"
        info = self.get_metrics(TEST_METRICS)
        self.log.info(info)

    def get_scrub_corrupt_metrics(self):
        CORRUPT_METRICS = "engine_pool_scrubber_corruption_current"
        info = self.get_metrics(CORRUPT_METRICS)
        self.log.info(info)

    def get_csum_total_metrics(self):
        CSUM_METRICS = "engine_pool_scrubber_csums_total"
        info = self.get_metrics(CSUM_METRICS)
        self.log.info(info)

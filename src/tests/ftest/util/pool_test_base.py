#!/usr/bin/python
"""
(C) Copyright 2020-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers


class PoolTestBase(TestWithServers):
    """Base pool test class.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()

        super().setUp()
        self.dmg = self.get_dmg_command()
        self.pool = []

    def check_pool_creation(self, max_duration):
        """Check the duration of each pool creation meets the requirement.

        Args:
            max_duration (int): max pool creation duration allowed in seconds

        """
        durations = []
        for index, pool in enumerate(self.pool):
            start = float(time.time())
            pool.create()
            durations.append(float(time.time()) - start)
            self.log.info(
                "Pool %s creation: %s seconds", index + 1, durations[-1])

        exceeding_duration = 0
        for index, duration in enumerate(durations):
            if duration > max_duration:
                exceeding_duration += 1

        self.assertEqual(
            exceeding_duration, 0,
            "Pool creation took longer than {} seconds on {} pool(s)".format(
                max_duration, exceeding_duration))

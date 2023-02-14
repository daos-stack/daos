"""
(C) Copyright 2020-2023 Intel Corporation.

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

    def check_pool_creation(self, pools, max_duration, offset=1, durations=None):
        """Check the duration of each pool creation meets the requirement.

        Args:
            pools (list): list of TestPool objects to create
            max_duration (int): max pool creation duration allowed in seconds
            offset (int, optional): pool index offset. Defaults to 1.
            durations (list, optional): list of other pool create durations to include in the check.
                Defaults to None.
        """
        if durations is None:
            durations = []
        for index, pool in enumerate(pools):
            durations.append(self.time_pool_create(index + offset, pool))

        exceeding_duration = 0
        for index, duration in enumerate(durations):
            if duration > max_duration:
                exceeding_duration += 1

        if exceeding_duration:
            self.fail(
                "Pool creation took longer than {} seconds on {} pool(s)".format(
                    max_duration, exceeding_duration))

    def time_pool_create(self, number, pool):
        """Time how long it takes to create a pool.

        Args:
            number (int): pool number in the list
            pool (TestPool): pool to create

        Returns:
            float: number of seconds elapsed during pool create

        """
        start = float(time.time())
        pool.create()
        duration = float(time.time()) - start
        self.log.info("Pool %s creation: %s seconds", number, duration)
        return duration

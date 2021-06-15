#!/usr/bin/python
"""
(C) Copyright 2020-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from apricot import TestWithServers
from general_utils import bytes_to_human, human_to_bytes
from server_utils import ServerFailed


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

    def get_pool_params(self, scm_ratio=6, nvme_ratio=90, min_targets=1,
                        quantity=1, svcn=None, targets=None):
        """Get the parameters for a TestPool object.

        Args:
            scm_ratio (int, optional): when creating a pool with NVMe
                (nvme_ratio > 0) this defines the pool SCM size as a percentage
                of the pool NVMe size. When creating a pool without NVMe
                (nvme_ratio == 0) this defines the pool SCM size as a percentage
                of the available SCM storage. Defaults to 6.
            nvme_ratio (int, optional): this defines the pool NVMe size as a
                percentage of the available NVMe storage, e.g. 80. If set to 0
                the pool is created with SCM only. Defaults to 90.
            min_targets (int, optional): the minimum number of targets per
                engine that can be configured. Defaults to 1.
            quantity (int, optional): Number of pools to account for in the size
                calculations. The pool size returned is only for a single pool.
                Defaults to 1.
            svcn (int, optional): [description]. Defaults to None.
            targets (list, optional): [description]. Defaults to None.

        Returns:
            dict: the parameters for a TestPool object.

        """
        # Get the autosized single pool params
        pool_params = super().get_pool_params(
            scm_ratio=scm_ratio, nvme_ratio=nvme_ratio, min_targets=min_targets,
            quantity=quantity)

        # Add the other non-autosized TestPool parameters
        pool_params["svcn"] = svcn
        pool_params["target_list"] = targets

        return pool_params

    def add_pool_with_params(self, pool_params, quantity=1):
        """Add TestPools to the pool list with the specified parameters.

        Args:
            pool_params ([type]): [description]
            quantity (int, optional): [description]. Defaults to 1.
        """
        for _ in range(quantity):
            self.pool.append(self.get_pool(create=False))
            for name in pool_params:
                pool_param = getattr(self.pool[-1], name)
                pool_param.update(pool_params[name])

    def add_autosized_pool(self, scm_ratio=6, nvme_ratio=90, min_targets=1,
                           quantity=1, svcn=None, targets=None):
        """Add TestPools to the pool list.

        Args:
            scm_ratio (int, optional): [description]. Defaults to 6.
            nvme_ratio (int, optional): [description]. Defaults to 90.
            min_targets (int, optional): [description]. Defaults to 1.
            quantity (int, optional): [description]. Defaults to 1.
            svcn ([type], optional): [description]. Defaults to None.
            targets ([type], optional): [description]. Defaults to None.
        """
        # Get the autosized single pool params
        pool_params = self.get_pool_params(
            scm_ratio=scm_ratio, nvme_ratio=nvme_ratio, min_targets=min_targets,
            quantity=quantity)

        # Create and configure the requested TestPool object
        self.add_pool_with_params(pool_params, quantity)

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

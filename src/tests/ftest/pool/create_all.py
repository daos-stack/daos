#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from pool_test_base import PoolTestBase
import json


class PoolCreateAllTest(PoolTestBase):
    # pylint: disable=too-few-public-methods
    """Tests pool create all basics

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a SimpleCreatePoll object."""
        super().__init__(*args, **kwargs)

    def setUp(self):
        """Set up the test case."""
        super().setUp()
        self.update_available_bytes()

    def update_available_bytes(self):
        """Update the available size of the tiers storage."""
        self.log.info("Retrieving available size")

        self.scm_bytes = 0
        self.smd_bytes = 0

        self.assertGreater(len(self.server_managers), 0, "No server managers")

        try:
            result = self.server_managers[0].dmg.storage_query_usage()
        except CommandFailure as error:
            self.fail("dmg command failed: {}".format(error))

        for host_storage in result["response"]["HostStorage"].values():
            for scm_devices in host_storage["storage"]["scm_namespaces"]:
                self.scm_bytes += scm_devices["mount"]["avail_bytes"]
            for nvme_device in host_storage["storage"]["nvme_devices"]:
                for smd_device in nvme_device["smd_devices"]:
                    self.smd_bytes += smd_device["avail_bytes"]

        self.log.info(f"Available Bytes: scm={self.scm_bytes}, smd={self.smd_bytes}")

    def check_available_bytes(self, scm_bytes, smd_bytes, delta_scm=0, delta_smd=0):
        """Check the available size of the tiers storage."""
        self.log.info("Checking available tiers storage")
        self.assertLessEqual(abs(self.scm_bytes - scm_bytes), delta_scm,
                f"Invalid SCM size: want={self.scm_bytes}, got={scm_bytes}")
        self.assertLessEqual(abs(self.smd_bytes - smd_bytes), delta_smd,
                f"Invalid SMD size: want={self.smd_bytes}, got={smd_bytes}")

    def test_one_pool(self):
        """Test basic pool creation with full storage

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=pool_create_tests,create_all_pool
        """

        self.log.info("Creating a pool with 100% of the available storage")
        self.add_pool_qty(1, namespace="/run/pool/*", create=False)
        self.pool[0].size.update("100%")
        self.pool[0].create()

        self.log.info("Checking pool size")
        self.pool[0].get_info()
        tier_bytes = self.pool[0].info.pi_space.ps_space.s_total
        self.check_available_bytes(tier_bytes[0], tier_bytes[1])

        self.update_available_bytes()
        self.check_available_bytes(0, 0)

    def test_two_pools(self):
        """Test pool creation of two pools with 50% and 100% of the available storage

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=pool_create_tests,create_all_pool
        """

        self.add_pool_qty(2, namespace="/run/pool/*", create=False)
        self.pool[0].size.update("50%")
        self.pool[1].size.update("100%")

        self.log.info("Creating a pool with 50% of the available storage")
        self.pool[0].create()
        self.pool[0].get_info()
        tiers_bytes = [self.pool[0].info.pi_space.ps_space.s_total]

        self.log.info("Checking pool size")
        delta = 1 << 20
        self.check_available_bytes(tiers_bytes[0][0]*2, tiers_bytes[0][1]*2, delta, 0)
        self.update_available_bytes()

        self.log.info("Creating a pool with 100% of the available storage")
        self.pool[1].create()
        self.pool[1].get_info()
        tiers_bytes.append(self.pool[1].info.pi_space.ps_space.s_total)

        self.log.info("Checking pool size")
        self.check_available_bytes(tiers_bytes[1][0], tiers_bytes[1][1], delta, 0)
        self.update_available_bytes()
        self.check_available_bytes(0, 0)

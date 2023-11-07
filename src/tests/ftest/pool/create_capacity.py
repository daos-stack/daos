"""
(C) Copyright 2021-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import sys
import time

from apricot import TestWithServers
from general_utils import get_display_size, human_to_bytes
from server_utils import ServerFailed
from test_utils_pool import add_pool, check_pool_creation


class PoolCreateCapacityTests(TestWithServers):
    # pylint: disable=too-few-public-methods
    """Pool create tests.

    All of the tests verify pool create performance with 7 servers and 1 client.
    Each server should be configured with full compliment of NVDIMMs and SSDs.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a PoolCreateCapacityTests object."""
        super().__init__(*args, **kwargs)

        self.pool_scm_bytes = 0
        self.pool_nvme_bytes = 0
        self.pool_ratio = 0

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()

        super().setUp()

        self.pool_scm_bytes = human_to_bytes(self.params.get("scm_size", "/run/pool/*", 0))
        self.pool_nvme_bytes = human_to_bytes(self.params.get("nvme_size", "/run/pool/*", 0))

        md_on_dev = "md_on_scm"
        if self.server_managers[0].manager.job.using_control_metadata:
            md_on_dev = "md_on_ssd"
        self.pool_ratio = self.params.get(md_on_dev, "/run/pool/ratio/*", 0)

    def get_available_storage(self):
        """Returns the largest available storage of the tiers storage

        Returns a two elements tuple defining the SCM and NVMe storage space which could be used for
        storing a pool.

        Returns:
            tuple: SCM and NVMe usable storage.
        """
        self.log.info("Retrieving available size")
        result = self.get_dmg_command().storage_query_usage()

        scm_engine_bytes = {}
        nvme_engine_bytes = {}
        for host_storage in result["response"]["HostStorage"].values():

            for scm_device in host_storage["storage"]["scm_namespaces"]:
                rank = scm_device["mount"]["rank"]
                if rank not in scm_engine_bytes:
                    scm_engine_bytes[rank] = 0
                scm_engine_bytes[rank] += scm_device["mount"]["avail_bytes"]

            if host_storage["storage"]["nvme_devices"] is None:
                continue

            for nvme_device in host_storage["storage"]["nvme_devices"]:
                if nvme_device["smd_devices"] is None:
                    continue
                for smd_device in nvme_device["smd_devices"]:
                    if smd_device["dev_state"] != "NORMAL":
                        continue
                    rank = smd_device["rank"]
                    if rank not in nvme_engine_bytes:
                        nvme_engine_bytes[rank] = 0
                    nvme_engine_bytes[rank] += smd_device["avail_bytes"]

        scm_bytes = sys.maxsize
        for size in scm_engine_bytes.values():
            scm_bytes = min(scm_bytes, size)
        if scm_bytes == sys.maxsize:
            scm_bytes = 0

        nvme_bytes = sys.maxsize
        for size in nvme_engine_bytes.values():
            nvme_bytes = min(nvme_bytes, size)
        if nvme_bytes == sys.maxsize:
            nvme_bytes = 0

        return scm_bytes, nvme_bytes

    def get_pool_count(self, ratio, scm_bytes, nvme_bytes):
        """TODO"""
        pool_scm_count = int((scm_bytes * ratio) / (self.pool_scm_bytes * 100))
        self.log.debug(f'>>> SPY-001: pool_scm_count={pool_scm_count} scm_bytes={scm_bytes} ratio={ratio} pool_scm_bytes={self.pool_scm_bytes}')
        pool_nvme_count = int((nvme_bytes * ratio) / (self.pool_nvme_bytes * 100))
        self.log.debug(f'>>> SPY-002: pool_nvme_count={pool_nvme_count} nvme_bytes={nvme_bytes} ratio={ratio} pool_nvme_bytes={self.pool_nvme_bytes}')

        return min(pool_scm_count, pool_nvme_count)

    def test_create_pool_quantity(self):
        """JIRA ID: DAOS-5114 / SRS-2 / SRS-4.

        Test Description:
            Create 200 pools on all of the servers.
            Perform an orderly system shutdown via cmd line (dmg).
            Restart the system via cmd line tool (dmg).
            Verify that DAOS is ready to accept requests within 2 minutes.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=pool
        :avocado: tags=PoolCreateCapacityTests,test_create_pool_quantity
        """
        # FIXME DAOS-14528: Comments should be updated according to the ratio of the PR.
        # Create some number of pools each using a equal amount of 60% of the
        # available capacity, e.g. 0.6% for 100 pools.
        storage = self.get_available_storage()
        self.log.debug("Available storage for pools:")
        self.log.debug("  - SCM:  %s", get_display_size(storage[0]))
        self.log.debug("  - NVMe: %s", get_display_size(storage[1]))
        pool_count = self.get_pool_count(self.pool_ratio, *storage)

        # Define all the pools with the same size defined in the test yaml
        self.log_step('Defining {} pools'.format(pool_count))
        pools = []
        for _ in range(pool_count):
            pools.append(add_pool(self, create=False))

        # Create all the pools
        self.log_step('Creating {} pools (dmg pool create)'.format(pool_count))
        self.get_dmg_command().server_set_logmasks("DEBUG", raise_exception=False)
        check_pool_creation(self, pools, 30)
        self.get_dmg_command().server_set_logmasks(raise_exception=False)

        # Verify DAOS can be restarted in less than 2 minutes
        self.log_step('Stopping all engines (dmg system stop)')
        try:
            self.server_managers[0].system_stop()
        except ServerFailed as error:
            self.fail(error)

        start = float(time.time())
        self.log_step('Starting all engines (dmg system start)')
        try:
            self.server_managers[0].system_start()
        except ServerFailed as error:
            self.fail(error)

        duration = float(time.time()) - start
        self.log_step('Verifying all engines started in 120 seconds: {}'.format(duration))
        if duration > 120:
            self.fail("DAOS not ready to accept requests within 2 minutes after restart")

        # Verify all the pools exists after the restart
        self.log_step('Verifying all {} pools exist after engine restart'.format(pool_count))
        self.get_dmg_command().timeout = 360
        pool_uuids = self.get_dmg_command().get_pool_list_uuids(no_query=True)
        detected_pools = [uuid.lower() for uuid in pool_uuids]
        missing_pools = []
        for pool in pools:
            pool_uuid = pool.uuid.lower()
            if pool_uuid not in detected_pools:
                missing_pools.append(pool_uuid)
        if missing_pools:
            self.fail(
                'The following created pools were not detected in the pool '
                'list after rebooting the servers:\n  [{}]: {}'.format(
                    len(missing_pools), ", ".join(missing_pools)))
        if len(pools) != len(detected_pools):
            self.fail('Incorrect number of pools detected after rebooting the servers')
        self.log_step('Test passed')

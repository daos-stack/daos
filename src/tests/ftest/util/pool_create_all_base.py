"""
(C) Copyright 2022-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import sys

from avocado.core.exceptions import TestFail

from apricot import TestWithServers
from general_utils import bytes_to_human


class PoolCreateAllTestBase(TestWithServers):
    """Base class for testing pool creation with percentage storage.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a PoolCreateAllBaseTests object."""
        super().__init__(*args, **kwargs)

        self.dmg = None
        self.engines_count = 0

    def setUp(self):
        """Set up each test case."""
        super().setUp()

        server_manager = self.server_managers[0]
        self.dmg = server_manager.dmg
        self.engines_count = server_manager.engines

    def get_usable_bytes(self):
        """Returns the usable bytes of the tiers storage

        Returns a two elements tuple defining the SCM and NVMe storage space which could be used for
        storing the data of a pool.

        Returns:
            tuple: SCM and NVMe usable space.
        """
        self.log.info("Retrieving available size")
        result = self.dmg.storage_query_usage()

        scm_engine_bytes = sys.maxsize
        nvme_engine_bytes = sys.maxsize
        host_size = 0
        for host_storage in result["response"]["HostStorage"].values():
            host_size += len(host_storage["hosts"].split(','))

            scm_bytes = 0
            for scm_devices in host_storage["storage"]["scm_namespaces"]:
                scm_bytes += scm_devices["mount"]["usable_bytes"]
            scm_engine_bytes = min(scm_engine_bytes, scm_bytes)

            if host_storage["storage"]["nvme_devices"] is None:
                continue

            nvme_bytes = 0
            for nvme_device in host_storage["storage"]["nvme_devices"]:
                if nvme_device["smd_devices"] is None:
                    continue
                for smd_device in nvme_device["smd_devices"]:
                    if smd_device["dev_state"] == "NORMAL":
                        nvme_bytes += smd_device["usable_bytes"]
            nvme_engine_bytes = min(nvme_engine_bytes, nvme_bytes)

        if scm_engine_bytes == sys.maxsize:
            scm_engine_bytes = 0
        if nvme_engine_bytes == sys.maxsize:
            nvme_engine_bytes = 0

        return host_size * scm_engine_bytes, host_size * nvme_engine_bytes

    def check_pool_full_storage(self, scm_delta_bytes, nvme_delta_bytes=None, ranks=None):
        """Check the creation of one pool with all the storage capacity.

        Create a pool with all the capacity of all servers.  Check that a pool could not be created
        with more SCM or NVME capacity.  The list of rank to use for creating pools could be given.

        Args:
            scm_delta_bytes (int): Oversubscribing storage space to add to the SCM storage.
            nvme_delta_bytes (int, optional): Oversubscribing storage space to add to the NVMe
                storage.  Defaults to None.
            ranks (list, optional): List of rank used for creating pools.  Defaults to None.
        """
        pool_count = 4 if nvme_delta_bytes is None else 5
        self.add_pool_qty(pool_count, create=False)
        pool_idx = len(self.pool) - pool_count

        self.log.info("Creating a pool with all the available storage: size=100%")
        self.pool[pool_idx].size.update("100%", "pool[{}].size".format(pool_idx))
        if ranks is not None:
            self.pool[pool_idx].target_list.update(ranks, "pool[{}].target_list".format(pool_idx))
        self.pool[pool_idx].create()
        self.pool[pool_idx].get_info()
        tier_bytes = self.pool[pool_idx].info.pi_space.ps_space.s_total
        if ranks is not None:
            wait_ranks = sorted(ranks)
            data = self.dmg.pool_query(self.pool[pool_idx].identifier, show_enabled=True)
            got_ranks = sorted(data['response']['enabled_ranks'])
            self.assertListEqual(
                wait_ranks,
                got_ranks,
                "Pool with invalid ranks: wait={} got={}".format(wait_ranks, got_ranks))
        self.log.info("Pool created: scm_size=%d, nvme_size=%d", *tier_bytes)
        self.pool[pool_idx].destroy()
        pool_idx += 1

        rank_count = len(ranks) if ranks is not None else self.engines_count
        dmg_scm_size = tier_bytes[0] // rank_count
        dmg_nvme_size = tier_bytes[1] // rank_count
        self.assertEqual(
            dmg_nvme_size == 0,
            nvme_delta_bytes is None,
            "Invalid function call: no NVME delta with usable NVMe storage")

        self.log.info(
            "Creating a pool with all the available storage: scm_size=%d, nvme_size=%d",
            dmg_scm_size,
            dmg_nvme_size)
        self.pool[pool_idx].scm_size.update(dmg_scm_size, "pool[{}].scm_size", pool_idx)
        if dmg_nvme_size > 0:
            self.pool[pool_idx].nvme_size.update(dmg_nvme_size, "pool[{}].nvme_size", pool_idx)
        if ranks is not None:
            self.pool[pool_idx].target_list.update(ranks, "pool[{}].target_list".format(pool_idx))
        self.pool[pool_idx].create()
        self.pool[pool_idx].destroy()
        pool_idx += 1

        self.log.info(
            "Creating a pool with SCM oversubscription: scm_size=%d nvme_size=%d",
            dmg_scm_size + scm_delta_bytes,
            dmg_nvme_size)
        self.pool[pool_idx].scm_size.update(
            dmg_scm_size + scm_delta_bytes,
            "pool[{}].scm_size".format(pool_idx))
        if dmg_nvme_size > 0:
            self.pool[pool_idx].nvme_size.update(
                dmg_nvme_size,
                "pool[{}].nvme_size".format(pool_idx))
        if ranks is not None:
            self.pool[pool_idx].target_list.update(ranks, "pool[{}].target_list".format(pool_idx))
        error_msg = r"Pool should not be created: SCM oversubscription"
        with self.assertRaises(TestFail, msg=error_msg) as context_manager:
            self.pool[pool_idx].create()
        self.assertIn(
            "DER_NOSPACE",
            str(context_manager.exception),
            "Pool creation failed with invalid error message")
        pool_idx += 1

        if dmg_nvme_size > 0:
            self.log.info(
                "Creating a pool with NVME oversubscription: scm_size=%d, nvme_size=%d",
                dmg_scm_size,
                dmg_nvme_size + nvme_delta_bytes)
            self.pool[pool_idx].scm_size.update(dmg_scm_size, "pool[{}].scm_size".format(pool_idx))
            self.pool[pool_idx].nvme_size.update(
                dmg_nvme_size + nvme_delta_bytes,
                "pool[{}].nvme_size".format(pool_idx))
            if ranks is not None:
                self.pool[pool_idx].target_list.update(
                    ranks,
                    "pool[{}].target_list".format(pool_idx))
            error_msg = r"Pool should not be created: NVME oversubscription"
            with self.assertRaises(TestFail, msg=error_msg) as context_manager:
                self.pool[pool_idx].create()
            self.assertIn(
                "DER_NOSPACE",
                str(context_manager.exception),
                "Pool creation failed with invalid error message")
            pool_idx += 1

        self.log.info(
            "Creating a pool with 100%% of the available storage: scm_size=%d, nvme_size=%d",
            dmg_scm_size,
            dmg_nvme_size)
        self.pool[pool_idx].scm_size.update(dmg_scm_size, "pool[{}].scm_size".format(pool_idx))
        if dmg_nvme_size > 0:
            self.pool[pool_idx].nvme_size.update(
                dmg_nvme_size,
                "pool[{}].nvme_size".format(pool_idx))
        if ranks is not None:
            self.pool[pool_idx].target_list.update(ranks, "pool[{}].target_list".format(pool_idx))
        self.pool[pool_idx].create()
        self.pool[pool_idx].destroy()

    def check_pool_recycling(self, pool_count, scm_delta_bytes, nvme_delta_bytes=None):
        """Check the pool creation and destruction.

        Create and destroy a 100% pool for an arbitrary number of times.  At each iteration check if
        the size of the created pools are always the same.

        Args:
            pool_count (int): Number of pool to create and destroy.
            scm_delta_bytes (int): Allowed difference of the SCM pool storage.
            nvme_delta_bytes (int, optional): Allowed difference of the NVMe pool storage.  Defaults
                to None.
        """

        self.add_pool_qty(pool_count, namespace="/run/pool/*", create=False)

        first_pool_size = None
        for index in range(pool_count):
            self.log.info("Creating pool %d with all the available storage: size=100%%", index)
            self.pool[index].size.update("100%", "pool[0].size")
            self.pool[index].create()
            self.pool[index].get_info()
            s_total = self.pool[index].info.pi_space.ps_space.s_total
            pool_size = int(s_total[0]), int(s_total[1])
            self.log.info(
                "Pool %d created: scm_size=%d, nvme_size=%d", index, *pool_size)
            self.pool[index].destroy()

            if first_pool_size is None:
                first_pool_size = pool_size
                continue
            self.log.info(
                "Difference of size with the pool %d and the first pool:"
                " scm=%s (%d bytes), nvme=%s (%d bytes)",
                index,
                bytes_to_human(abs(pool_size[0] - first_pool_size[0])),
                pool_size[0] - first_pool_size[0],
                bytes_to_human(abs(pool_size[1] - first_pool_size[1])),
                pool_size[1] - first_pool_size[1])

            self.assertAlmostEqual(
                pool_size[0],
                first_pool_size[0],
                delta=scm_delta_bytes,
                msg="Pool {} with invalid SCM size".format(index))

            if nvme_delta_bytes is None:
                continue

            self.assertAlmostEqual(
                pool_size[1],
                first_pool_size[1],
                delta=nvme_delta_bytes,
                msg="Pool {} with invalid NVMe size".format(index))

    def check_pool_distribution(self, scm_delta_bytes, nvme_delta_bytes=None):
        """Check if the storage used on each host is more or less uniform.

        Check if the difference of SCM and NVMe storage space used, by a pool on each engine, is
        acceptable.

        Args:
            scm_delta_bytes (int): Allowed difference of SCM storage size used on each engine.
            nvme_delta_bytes (int, optional): Allowed difference of NVMe storage size used on each
                engine.  Defaults to None.
        """
        self.log.info("Retrieving available size")
        result = self.server_managers[0].dmg.storage_query_usage()

        scm_used_bytes = [sys.maxsize, 0]
        if nvme_delta_bytes is not None:
            nvme_used_bytes = [sys.maxsize, 0]
        for host_storage in result["response"]["HostStorage"].values():
            scm_bytes = 0
            for scm_devices in host_storage["storage"]["scm_namespaces"]:
                scm_bytes += scm_devices["mount"]["total_bytes"]
                scm_bytes -= scm_devices["mount"]["avail_bytes"]
            if scm_bytes < scm_used_bytes[0]:
                scm_used_bytes[0] = scm_bytes
            if scm_bytes > scm_used_bytes[1]:
                scm_used_bytes[1] = scm_bytes

            if nvme_delta_bytes is None:
                continue

            nvme_bytes = 0
            for nvme_device in host_storage["storage"]["nvme_devices"]:
                for smd_device in nvme_device["smd_devices"]:
                    if smd_device["dev_state"] != "NORMAL":
                        continue
                    nvme_bytes += smd_device["total_bytes"]
                    nvme_bytes -= smd_device["avail_bytes"]
            if nvme_bytes < nvme_used_bytes[0]:
                nvme_used_bytes[0] = nvme_bytes
            if nvme_bytes > nvme_used_bytes[1]:
                nvme_used_bytes[1] = nvme_bytes

        self.assertAlmostEqual(
            scm_used_bytes[0],
            scm_used_bytes[1],
            delta=scm_delta_bytes,
            msg="Data not evenly distributed among SCM devices")

        if nvme_delta_bytes is None:
            return

        self.assertAlmostEqual(
            nvme_used_bytes[0],
            nvme_used_bytes[1],
            delta=nvme_delta_bytes,
            msg="Data not evenly distributed among NVME devices")

    def check_pool_half_storage(self, scm_delta_bytes, nvme_delta_bytes=None):
        """Check the creation of one pool with half of the usable storage capacity.

        Create a pool with half the capacity of all servers.  Check the size of the pool created
        compared to the initial usable size.

        Args:
            scm_delta_bytes (int): Allowed difference of the SCM pool storage.
            nvme_delta_bytes (int): Allowed difference of the NVMe pool storage.
        """
        self.add_pool_qty(1, namespace="/run/pool/*", create=False)

        usable_bytes = self.get_usable_bytes()
        self.log.info("Usable bytes: scm_size=%d, nvme_size=%d", *usable_bytes)

        self.log.info("Creating pool with half of the available storage: size=50%")
        self.pool[0].size.update("50%")
        self.pool[0].create()
        self.pool[0].get_info()
        s_total = self.pool[0].info.pi_space.ps_space.s_total
        pool_size = (int(s_total[0]), int(s_total[1]))
        self.log.info("Pool created: scm_size=%d, nvme_size=%d", *pool_size)

        self.assertAlmostEqual(
            usable_bytes[0] // 2,
            pool_size[0],
            delta=scm_delta_bytes,
            msg="Pool with invalid SCM size")

        if nvme_delta_bytes is None:
            return

        self.assertAlmostEqual(
            usable_bytes[1] // 2,
            pool_size[1],
            delta=nvme_delta_bytes,
            msg="Pool with invalid NVME size")

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

        self.md_on_dev = ""
        self.start_duration_timeout = 0
        self.quantity = 0
        self.meta_bytes = 0
        self.data_bytes = 0

    def setUp(self):
        """Set up each test case."""
        # Create test-case-specific DAOS log files
        self.update_log_file_names()
        super().setUp()

        self.md_on_dev = "md_on_scm"
        if self.server_managers[0].manager.job.using_control_metadata:
            self.md_on_dev = "md_on_ssd"

        self.start_duration_timeout = self.params.get("start_duration", "/run/server_config/*", 0)
        self.quantity = self.params.get("quantity", "/run/pool/*", 0)
        self.meta_bytes = human_to_bytes(self.params.get("scm_size", "/run/pool/*", 0))
        self.data_bytes = human_to_bytes(self.params.get("nvme_size", "/run/pool/*", 0))

    def get_nsp_available_bytes(self, namespaces, namespace_id, cluster_bytes):
        """Returns the available storage of a given NVMe namespace.

        Args:
            namespaces (dict): dmg json command output defining the NVMe namespaces.
            namespace_id (int): Identifier of a namespace.
            cluster_bytes (int): Size of an SPDK cluster.

        Returns:
            int: Available space of the given NVMe namespace.

        """
        for nsp in namespaces:
            if nsp["id"] != namespace_id:
                continue
            return int(nsp["size"] / cluster_bytes) * cluster_bytes
        self.fail(f"Namespace of id {namespace_id} could not be found")

    def get_available_storage(self, storage_usage):
        """Returns the largest available storage of the tiers storage.

        Returns a two elements tuple defining the SCM and NVMe storage space which can  be used for
        storing a pool.

        Args
            storage_usage (dict): dmg json command output defining the storage usage.

        Returns:
            tuple: SCM and NVMe usable storage.

        """
        scm_engine_bytes = {}
        nvme_engine_bytes = {}
        for host_storage in storage_usage["response"]["HostStorage"].values():

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
                namespace_id = 0
                for smd_device in nvme_device["smd_devices"]:
                    namespace_id += 1
                    if smd_device["dev_state"] != "NORMAL":
                        continue
                    rank = smd_device["rank"]
                    roles = smd_device["roles"]
                    if not roles:
                        # md-on-scm has only one implicit data role
                        roles = "data"
                    if rank not in nvme_engine_bytes:
                        nvme_engine_bytes[rank] = {}
                    if roles not in nvme_engine_bytes[rank]:
                        nvme_engine_bytes[rank][roles] = 0
                    namespaces = nvme_device["namespaces"]
                    cluster_bytes = smd_device["cluster_size"]
                    nvme_engine_bytes[rank][roles] += self.get_nsp_available_bytes(
                        namespaces, namespace_id, cluster_bytes)

        scm_bytes = min(scm_engine_bytes.values(), default=0)

        nvme_bytes = {}
        for it in nvme_engine_bytes.values():
            for roles, roles_bytes in it.items():
                if roles not in nvme_bytes:
                    nvme_bytes[roles] = roles_bytes
                nvme_bytes[roles] = min(nvme_bytes[roles], roles_bytes)

        return scm_bytes, nvme_bytes

    def get_nvme_attr(self, storage_usage, attr_name):
        """Returns the first json value of a given NVMe device attribute.

        Args:
            storage_usage (dict): dmg json command output defining the storage usage.
            attr_name (str): Name of a NVMe devices attribute.

        Returns:
            object: json value of given attribute.

        """
        for host_storage in storage_usage["response"]["HostStorage"].values():
            for nvme_device in host_storage["storage"]["nvme_devices"]:
                if nvme_device["smd_devices"] is None:
                    continue
                for smd_device in nvme_device["smd_devices"]:
                    if smd_device["dev_state"] != "NORMAL":
                        continue
                    return smd_device[attr_name]
        raise AttributeError(f"SMD device attribute '{attr_name}' not found")

    def get_cluster_bytes(self, storage_usage):
        """Returns the size of one SPDK cluster.

        Args:
            storage_usage (dict): dmg json command output defining the storage usage.

        Returns:
            int: size in bytes of a SDPK cluster.

        """
        try:
            cluster_bytes = self.get_nvme_attr(storage_usage, "cluster_size")
        except AttributeError:
            self.fail("SPDK Cluster size can not be retrieved")
        return cluster_bytes

    def get_rdb_bytes(self, storage_usage):
        """Returns the size of one DAOS RDB.

        Args:
            storage_usage (dict): dmg json command output defining the storage usage.

        Returns:
            int: size in bytes of one DAOS RDB.

        """
        try:
            rdb_bytes = self.get_nvme_attr(storage_usage, "rdb_size")
        except AttributeError:
            self.fail("DAOS RDB size can not be retrieved")
        return rdb_bytes

    def get_rdb_wal_bytes(self, storage_usage):
        """Returns the size of one DAOS RDB WAL.

        Args:
            storage_usage (dict): dmg json command output defining the storage usage.

        Returns:
            int: size in bytes of a DAOS RDB WAL.

        """
        try:
            wal_bytes = self.get_nvme_attr(storage_usage, "rdb_wal_size")
        except AttributeError:
            self.fail("DAOS RDB WAL size can not be retrieved")
        return wal_bytes

    def get_nvme_roles(self, storage_usage):
        """Returns the list of NVMe roles which can are assigned to NVMe bdev.

        Args:
            storage_usage (dict): dmg json command output defining the storage usage.

        Returns:
            list: list of NVMe bdev roles.

        """
        for host_storage in storage_usage["response"]["HostStorage"].values():
            nvme_roles = []
            for nvme_device in host_storage["storage"]["nvme_devices"]:
                if nvme_device["smd_devices"] is None:
                    continue
                for smd_device in nvme_device["smd_devices"]:
                    if smd_device["dev_state"] != "NORMAL":
                        continue
                    roles = smd_device["roles"]
                    if roles and roles not in nvme_roles:
                        nvme_roles.append(roles)
            return nvme_roles

    def bytes_to_clusters(self, cluster_bytes, size_bytes):
        """Converts a size in bytes in a number of SPDK clusters.

        Args:
            cluster_bytes (int): size of one SPDK cluster.
            size_bytes (int): storage space in bytes.

        Returns:
            int: number of SPDK cluster.

        """
        size_clusters = int(size_bytes / cluster_bytes)
        if size_clusters == 1 or size_bytes % cluster_bytes != 0:
            size_clusters += 1
        return size_clusters

    def get_md_on_scm_quantity(self, storage_usage, nvme_avail_bytes):
        """Returns the maximal quantity of pool which can created with md-on-scm DAOS server.

        Note:
            The algorithm of this function assume that the test is running over server with
            PMEM-SCM.  With such systems the number of pools which can be stored into the SCM is
            always greater than the number which can be stored into the NVMe devices.

        Args
            storage_usage (dict): dmg json command output defining the storage usage.
            nvme_avail_bytes (dict): NVMe available storage for each set of NVMe bdev roles.

        Returns
            int: Maximal quantity of pool which can be created.

        """
        cluster_bytes = self.get_cluster_bytes(storage_usage)
        data_clusters = self.bytes_to_clusters(cluster_bytes, self.data_bytes)
        nvme_avail_clusters = int(nvme_avail_bytes / cluster_bytes)
        return int(nvme_avail_clusters / data_clusters)

    def get_md_on_ssd_quantity(self, storage_usage, scm_avail_bytes, nvme_avail_bytes):
        """Returns the maximal quantity of pool which can created with md-on-ssd DAOS server.

        Note:
            The algorithm of this function assume that only one target is running on each engine.

        Args
            storage_usage (dict): dmg json command output defining the storage usage.
            nvme_avail_bytes (dict): NVMe available storage for each set of NVMe bdev roles.

        Returns
            int: Maximal quantity of pool which can be created.

        """
        rdb_bytes = self.get_rdb_bytes(storage_usage)
        scm_pool_bytes = self.meta_bytes + rdb_bytes
        # NOTE Keep 10% of the SCM for the memory fragmentation and DAOS system space
        scm_avail_bytes = int(scm_avail_bytes * 0.9)
        scm_quantity = int(scm_avail_bytes / scm_pool_bytes)

        cluster_bytes = self.get_cluster_bytes(storage_usage)
        nvme_quantity = sys.maxsize
        for roles in self.get_nvme_roles(storage_usage):
            pool_clusters = 0
            for role in roles.split(','):
                if role == 'wal':
                    # NOTE Assuming that meta is small (< 3GiB)
                    meta_wal = 2 * self.meta_bytes
                    pool_clusters += self.bytes_to_clusters(cluster_bytes, meta_wal)
                    pool_clusters += self.bytes_to_clusters(
                        cluster_bytes, self.get_rdb_wal_bytes(storage_usage))
                elif role == 'meta':
                    pool_clusters += self.bytes_to_clusters(cluster_bytes, self.meta_bytes)
                    pool_clusters += self.bytes_to_clusters(cluster_bytes, rdb_bytes)
                elif role == 'data':
                    pool_clusters += self.bytes_to_clusters(cluster_bytes, self.data_bytes)
                else:
                    self.fail(f"Unknown MD on SSD role {role}")
            nvme_avail_clusters = int(nvme_avail_bytes[roles] / cluster_bytes)
            nvme_quantity = min(nvme_quantity, int(nvme_avail_clusters / pool_clusters))

        return min(scm_quantity, nvme_quantity)

    def test_create_pool_quantity(self):
        """JIRA ID: DAOS-5114 / SRS-2 / SRS-4.

        Test Description:
            Create 200 pools on all of the servers.
            Perform an orderly system shutdown via cmd line (dmg).
            Restart the system via cmd line tool (dmg).
            Verify that DAOS is ready to accept requests within 2 minutes.

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=pool
        :avocado: tags=PoolCreateCapacityTests,test_create_pool_quantity
        """
        self.log_step("Retrieving storage usage")
        storage_usage = self.get_dmg_command().storage_query_usage()

        scm_avail_bytes, nvme_avail_bytes = self.get_available_storage(storage_usage)
        self.log.debug("Available storage for pools:")
        self.log.debug("  - SCM:  %s", get_display_size(scm_avail_bytes))
        self.log.debug("  - NVMe: %s", ", ".join(
            f"{role}={get_display_size(avail_bytes)}"
            for role, avail_bytes in nvme_avail_bytes.items()))

        # Evaluate the maximal quantity of pools which can be created
        if self.md_on_dev == "md_on_scm":
            quantity_max = self.get_md_on_scm_quantity(storage_usage, nvme_avail_bytes['data'])
        else:
            quantity_max = self.get_md_on_ssd_quantity(
                storage_usage, scm_avail_bytes, nvme_avail_bytes)
        self.log.debug("At most %d pools can be created", quantity_max)
        if self.quantity > quantity_max:
            self.log.info(
                "Reducing pool quantity from %d -> %d due to insufficient storage capacity",
                self.quantity, quantity_max)
            self.quantity = quantity_max

        # Define all the pools with the same size defined in the test yaml
        self.log_step('Defining {} pools'.format(self.quantity))
        pools = []
        for _ in range(self.quantity):
            pools.append(add_pool(self, create=False))

        # Create all the pools
        self.log_step('Creating {} pools (dmg pool create)'.format(self.quantity))
        self.get_dmg_command().server_set_logmasks("DEBUG", raise_exception=False)
        check_pool_creation(self, pools, 30, 2)
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
        self.log_step(
            'Verifying all engines started in {} seconds: {}'
            .format(self.start_duration_timeout, duration))
        if duration > self.start_duration_timeout:
            self.fail(
                "DAOS not ready to accept requests within {} seconds after restart"
                .format(self.start_duration_timeout))

        # Verify all the pools exists after the restart
        self.log_step('Verifying all {} pools exist after engine restart'.format(self.quantity))
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

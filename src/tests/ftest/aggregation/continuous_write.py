"""
  (C) Copyright 2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import threading
import time

from command_utils_base import CommandFailure
from ior_test_base import IorTestBase
from ior_utils import IorCommand
from job_manager_utils import get_job_manager


class ContinuousWrite(IorTestBase):
    """Verify that aggregation works while new snapshots continuously fill up the NVMe space.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a ContinuousWrite object."""
        super().__init__(*args, **kwargs)
        self.aggregation_detected = False

    def run_ior_repeat(self, namespace, pool, container):
        """Repeatedly run IOR every second for 60 times or until aggregation is detected.

        Args:
            namespace (str): Namespace that defines block_size and transfer_size.
            pool (TestPool): Pool to use with IOR.
            container (TestContainer): Container to use with IOR.
        """
        ior_cmd = IorCommand(namespace=namespace)
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(self.server_group, pool, container.identifier)
        testfile = os.path.join(os.sep, "test_file_1")
        ior_cmd.test_file.update(testfile)
        manager = get_job_manager(test=self, job=ior_cmd, subprocess=self.subprocess)
        manager.assign_hosts(
            self.hostlist_clients, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", namespace)
        manager.assign_processes(ppn=ppn)

        for count in range(60):
            # In this test, use "##" in the log message because a thread is used and the logs are
            # written in unpredictable manner. Using "##" makes it easier to find the debug logs
            # without using search.
            msg = f"## Run IOR. Count = {count}. Aggregation detected = {self.aggregation_detected}"
            self.log.info(msg)
            try:
                manager.run()
            except CommandFailure as error:
                self.log.info(error)
            if self.aggregation_detected:
                self.log.info("## Aggregation detected. End IOR write loop.")
                break
            time.sleep(1)

    def get_nvme_free(self, pool):
        """Get NVMe free size from dmg pool query output.

        Args:
            pool (TestPool): Pool to query the NVMe free size.

        Returns:
            int: NVMe free size in Bytes. If not found in the pool query output, returns None.

        """
        query_out = pool.query()
        tier_stats = query_out["response"]["tier_stats"]
        for tier_stat in tier_stats:
            if tier_stat["media_type"] == "nvme":
                return tier_stat["free"]

        return None

    def test_continuous_write(self):
        """Test that free space increase (aggregation) occurs while IOR is continuously running.

        Aggregation should occur when the NVMe space becomes low. Test that aggregation occurs when
        NVMe space becomes low due to many snapshots.

        Testing aggregation properly isn't straightforward, especially using automation because the
        algorithm is unclear and unpredictable. In this test, we focus on the occurrence of
        aggregation while IOR write is continuously running at certain rate. I.e., We don't test the
        amount of space reclaimed or different block sizes that fill up the NVMe at different rates.

        Jira ID: DAOS-14204

        1. Create a pool and a container.
        2. Write a several MB file and repeatedly overwrite it with a thread.
        3. Monitor the NVMe Free space with dmg pool query and verify that the free space goes up
        at certain point.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=aggregation
        :avocado: tags=ContinuousWrite,test_continuous_write
        """
        # 1. Create a pool and a container.
        self.log_step("Create a pool and a container.")
        pool = self.get_pool()
        container = self.get_container(pool=pool)

        # 2. Write a several MB file and repeatedly overwrite it with a thread.
        self.log_step("Write a several MB file and repeatedly overwrite it with a thread.")
        kwargs = {
            "namespace": "/run/ior/*",
            "pool": pool,
            "container": container
        }
        thread = threading.Thread(target=self.run_ior_repeat, kwargs=kwargs)
        thread.start()

        # 3 Monitor the NVMe Free space with dmg pool query and verify that the free space goes up.
        msg = ("Monitor the NVMe Free space with dmg pool query and verify that the free space "
               "goes up")
        self.log_step(msg)
        nvme_free_prev = self.get_nvme_free(pool=pool)

        for count in range(20):
            self.log.info("## Query NVMe Free space. Count = %d", count)
            nvme_free = self.get_nvme_free(pool=pool)
            msg = f"## NVMe Free Before = {nvme_free_prev:,} Byte; Now = {nvme_free:,} Byte"
            self.log.info(msg)
            if nvme_free > nvme_free_prev:
                # Free space increase (aggregation) detected.
                self.aggregation_detected = True
                msg = (f"## Aggregation detected. Before = {nvme_free_prev:,} Byte; "
                       f"Now = {nvme_free:,} Byte. End dmg pool query loop.")
                self.log.info(msg)
                break
            nvme_free_prev = nvme_free
            time.sleep(3)

        # If pool query loop ends first, wait for the IOR repeats in the thread.
        thread.join()

        if not self.aggregation_detected:
            self.fail("Aggregation wasn't detected!")

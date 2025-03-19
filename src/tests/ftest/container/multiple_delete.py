"""
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time

from ior_test_base import IorTestBase


class MultipleContainerDelete(IorTestBase):
    """Test class Description:
       Test that multiple container create/delete reclaims the pool space without leak.

    :avocado: recursive
    """

    def test_multiple_container_delete(self):
        """Jira ID: DAOS-3673

        Test Description:
            Purpose of this test is to verify the container delete
            returns all space used by a container without leak
        Use case:
            Create a pool spanning 4 servers.
            Capture the pool space.
            Create a POSIX container and fill it with IOR DFS Api
            Delete the container and repeat the above steps 50 times.
            Verify both the SCM and NVMe pool spaces are recovered

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=container
        :avocado: tags=MultipleContainerDelete,test_multiple_container_delete
        """
        self.add_pool(connect=False)

        out = []

        initial_scm_fs, initial_ssd_fs = self.get_pool_space()

        for loop in range(50):
            self.log.info("Create-Write-Destroy Iteration %d", loop)
            self.create_cont()
            self.ior_cmd.set_daos_params(self.pool, self.container.identifier)
            # If the transfer size is less than 4K, the objects are
            # inserted into SCM and anything greater goes to SSD
            self.run_ior_with_pool(create_cont=False)
            self.container.destroy()
            scm_fs, ssd_fs = self.get_pool_space()
            out.append("iter = {}, scm = {}, ssd = {}".format(loop + 1, scm_fs, ssd_fs))

        self.log.info("Initial Free Space")
        self.log.info("SCM = %d, NVMe = %d", initial_scm_fs, initial_ssd_fs)
        self.log.info("Free space after each cont create/del iteration")
        self.log.info("\n".join(out))
        final_scm_fs, final_ssd_fs = self.get_pool_space()
        self.log.info("Final free Space after all iters")
        self.log.info("SCM = %d, NVMe = %d", final_scm_fs, final_ssd_fs)

        self.log.info("Verifying NVMe space is recovered")
        if not self.pool.check_free_space(expected_nvme=initial_ssd_fs):
            self.fail("NVMe space is not recovered after 50 create-write-destroy iterations")

        # Verify SCM space recovery. About 198KB of the SCM free space isn't recovered
        # even after waiting for 180 sec, so apply the threshold. Considered not a bug.
        # DAOS-8643
        self.log.info("Verifying SCM space is recovered")
        scm_recovered = False
        # Based on the experiments, the recovery occurs in every 8 iterations. However,
        # since 50 is not divisible by 8, some data would remain in the disk right after
        # the 50th iteration. If we wait for several seconds, that remaining data will be
        # deleted (and we have 198KB left as mentioned above).
        scm_threshold = self.params.get("scm_threshold", "/run/*")
        for _ in range(5):
            final_scm_fs, _ = self.get_pool_space()
            scm_diff = initial_scm_fs - final_scm_fs
            if scm_diff <= scm_threshold:
                msg = ("SCM space was recovered. Initial = {}; Final = {}; "
                       "Threshold = {}; (Unit is in byte)").format(
                           initial_scm_fs, final_scm_fs, scm_threshold)
                self.log.info(msg)
                scm_recovered = True
                break
            time.sleep(10)

        if not scm_recovered:
            msg = ("SCM space wasn't recovered! Initial = {}, Final = {}, "
                   "Threshold = {}; (Unit is in byte.)".format(
                       initial_scm_fs, final_scm_fs, scm_threshold))
            self.fail(msg)

    def get_pool_space(self):
        """Get scm and ssd pool free space

        Returns:
            tuple: (scm_free_space (int), ssd_free_space (int))

        """
        if self.pool is not None:
            scm_index, ssd_index = 0, 1
            self.pool.connect()
            pool_info = self.pool.pool.pool_query()
            scm_fs = pool_info.pi_space.ps_space.s_free[scm_index]
            ssd_fs = pool_info.pi_space.ps_space.s_free[ssd_index]
            return scm_fs, ssd_fs

        self.log.error("****POOL is NONE*****")
        return 0, 0

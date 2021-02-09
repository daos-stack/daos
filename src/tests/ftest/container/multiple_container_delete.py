#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from ior_test_base import IorTestBase

class MultipleContainerDelete(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description:
       Test that multiple container create/delete reclaims
       the pool space without leak.

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
            Delete the container and repeat the above steps 1000
            times.
            Verify both the SCM and SSD pool spaces are recovered

        :avocado: tags=all,hw,large,full_regression,container
        :avocado: tags=multicontainerdelete
        """

        if self.pool is None:
            self.create_pool()
        self.pool.connect()

        out = []

        initial_scm_fs, initial_ssd_fs = self.get_pool_space()

        for i in range(1000):
            self.create_cont()
            self.ior_cmd.set_daos_params(self.server_group, self.pool,
                                         self.container.uuid)
            # If the transfer size is less than 4K, the objects are
            # inserted into SCM and anything greater goes to SSD
            self.run_ior_with_pool()
            self.container.destroy()
            scm_fs, ssd_fs = self.get_pool_space()
            out.append("iter = {}, scm = {}, ssd = {}".format(
                i+1, scm_fs, ssd_fs))

        self.log.info("Initial Free Space")
        self.log.info("SCM = {}, SSD = {}".format(
            initial_scm_fs, initial_ssd_fs))
        self.log.info("Free space after each cont create/del iteration")
        self.log.info("\n".join(out))
        final_scm_fs, final_ssd_fs = self.get_pool_space()
        self.log.info("Final free Space after all iters")
        self.log.info("SCM = {}, SSD = {}".format(
            final_scm_fs, final_ssd_fs))

        self.log.info("Verifying SSD space is recovered")
        self.log.info("{} == {}".format(final_ssd_fs, initial_ssd_fs))
        self.assertTrue(final_ssd_fs == initial_ssd_fs)

        # Uncomment the below verification once DAOS-3849 is fixed
        #self.log.info("Verifying SCM space is recovered")
        #self.log.info("{} == {}".format(final_scm_fs, initial_scm_fs)))
        #self.assertTrue(final_scm_fs == initial_scm_fs)

    def get_pool_space(self):
        """Get scm and ssd pool free space

        Returns:
            (scm_free_space (int), ssd_free_space (int))
        """
        if self.pool is not None:
            scm_index, ssd_index = 0, 1
            pool_info = self.pool.pool.pool_query()
            scm_fs = pool_info.pi_space.ps_space.s_free[scm_index]
            ssd_fs = pool_info.pi_space.ps_space.s_free[ssd_index]
            return scm_fs, ssd_fs
        else:
            self.log.error("****POOL is NONE*****")
            return 0, 0

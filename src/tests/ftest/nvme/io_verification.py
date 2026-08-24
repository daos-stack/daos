"""
  (C) Copyright 2020-2023 Intel Corporation.
  (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from dmg_utils import check_system_query_status
from ior_test_base import IorTestBase


class NvmeIoVerification(IorTestBase):
    """Test class for NVMe with IO tests.

    Test Class Description:
        Test IO on nvme with different pool sizes and different data size.

    :avocado: recursive
    """

    def test_nvme_server_restart(self):
        """Jira ID: DAOS-2650.

        Test Description:
            Test will run IOR with non standard transfer sizes for different set of pool sizes.
            Purpose is to verify io transaction to scm and nvme for different pool sizes when
            servers are restarted after write.

        Test Steps:
        1. Create a pool of specified size percentage.
        2. Create a container and run IOR write with specified transfer size. Some transfer size
        is smaller than 4096 and some are larger so that both SCM and NVMe are tested. See the test
        yaml for detail.
        3. Restart servers and verify that all servers restarted.
        4. Run IOR read to verify that reading the data written before the restart works.
        5. Destroy container and go to step 2. After all transfer sizes are tested, go to next.
        6. Destroy pool and go to step 1.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,daosio
        :avocado: tags=NvmeIoVerification,test_nvme_server_restart
        """
        ior_processes = self.params.get("np", '/run/ior/*')
        ior_transfer_size = self.params.get("tsize", '/run/ior/transfer_size/*/')
        ior_block_size = self.ior_cmd.block_size.value
        num_pools = self.params.get("num_pools", '/run/pool/*')
        ior_flag_write = self.params.get("write", '/run/ior/*/')
        ior_flag_read = self.params.get("read", '/run/ior/*/')
        job_manager = self.get_ior_job_manager_command()

        md_on_ssd = self.server_managers[0].manager.job.using_control_metadata

        # Loop for every pool size.
        for index in range(num_pools):
            self.log_step("Create a pool: pool_{}".format(index))
            self.pool = self.get_pool(namespace="/run/pool_{}/*".format(index))

            self.log_step("Query the pool for information.")
            self.pool.get_info()

            for tsize in ior_transfer_size:
                self.log_step("Run a test pass with transfer size = {} byte".format(tsize))
                # Get the current pool size.
                size_before_ior = self.pool.info

                self.log_step("Run ior write with the parameters specified for this pass.")
                self.ior_cmd.transfer_size.update(tsize)
                self.ior_cmd.flags.update(ior_flag_write)
                # If transfer size is less thank 1K update block size to 32K to keep it small
                if tsize <= 1000:
                    self.ior_cmd.block_size.update(32000)
                else:
                    self.ior_cmd.block_size.update(ior_block_size)
                container = self.get_container(self.pool)
                self.ior_cmd.set_daos_params(self.pool, container.identifier)
                self.run_ior(job_manager, ior_processes)

                self.log_step("Stop all servers.")
                self.get_dmg_command().system_stop(True)

                self.log_step("Start all servers")
                self.get_dmg_command().system_start()

                self.log_step("Check if all servers started as expected.")
                scan_info = self.get_dmg_command().system_query()
                if not check_system_query_status(scan_info):
                    self.fail("One or more servers crashed")

                self.log_step("Run IOR read to verify data written before server restart.")
                self.ior_cmd.flags.update(ior_flag_read)
                self.run_ior(job_manager, ior_processes)

                self.log_step("Verify IOR consumed the expected amount from the pool.")
                # Data in SCM are moved to NVMe in MD-on-SSD after restart, so we need to compare
                # the original and current pool usage on NVMe. verify_pool_size determines which
                # storage to use by checking whether self.ior_cmd.transfer_size is above 4096.
                if md_on_ssd and tsize < 4096:
                    self.log.info("MD-on-SSD with SCM - Update transfer size to >4096.")
                    self.ior_cmd.transfer_size.update(10000)
                self.verify_pool_size(size_before_ior, self.processes)

                self.log_step("Destroy container.")
                container.destroy()

            self.log_step("Destroy pool.")
            self.pool.destroy()

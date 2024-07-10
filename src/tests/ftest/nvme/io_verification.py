"""
  (C) Copyright 2020-2023 Intel Corporation.

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

    def test_nvme_io_verification(self):
        """Jira ID: DAOS-2649.

        Test Description:
            Test will run IOR with non standard transfer sizes for different
            set of pool sizes. Purpose is to verify io transaction to scm and
            nvme for different pool sizes under different situations.

        Use Cases:
            (1) Running IOR with different set of transfer size where first
            transfer size is < 4096 and then > 4096. Verify that data goes to
            scm if transfer size < 4096 and then it goes to nvme if transfer
            size is > 4096.
            (2) Repeat the case(1) with maximum nvme pool size that can be
            created.
            (3) Running IOR with different set of transfer size where the
            transfer size is > 4096 throughout. Verify that data goes to nvme
            as transfer size is > 4096.
            (4) Repeat the case(3) with maximum nvme pool size that can be
            created.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,daosio
        :avocado: tags=NvmeIoVerification,test_nvme_io_verification
        """
        ior_processes = self.params.get("np", '/run/ior/*')
        ior_transfer_size = self.params.get("tsize", '/run/ior/transfersize/*/')
        ior_block_size = self.ior_cmd.block_size.value
        ior_seq_pool_qty = self.params.get("ior_sequence_pool_qty", '/run/pool/*')
        job_manager = self.get_ior_job_manager_command()

        # Loop for every pool size
        for index in range(ior_seq_pool_qty):
            # Create and connect to a pool with namespace
            self.add_pool(namespace="/run/pool/pool_{}/*".format(index))

            # get pool info
            self.pool.get_info()

            for tsize in ior_transfer_size:
                # Get the current pool sizes
                size_before_ior = self.pool.info

                # Run ior with the parameters specified for this pass
                self.ior_cmd.transfer_size.update(tsize)
                # if transfer size is less thank 1K
                # update block size to 32K to keep it small
                if tsize <= 1000:
                    self.ior_cmd.block_size.update(32000)
                else:
                    self.ior_cmd.block_size.update(ior_block_size)
                container = self.get_container(self.pool)
                container.open()  # Workaround for pydaos handles
                self.ior_cmd.set_daos_params(self.pool, container.identifier)
                self.run_ior(job_manager, ior_processes)

                # Verify IOR consumed the expected amount from the pool
                self.verify_pool_size(size_before_ior, self.processes)

                # Destroy the container
                container.destroy()

            # destroy pool
            self.pool.destroy()

    def test_nvme_server_restart(self):
        """Jira ID: DAOS-2650.

        Test Description:
            Test will run IOR with non standard transfer sizes for different
            set of pool sizes. Purpose is to verify io transaction to scm and
            nvme for different pool sizes when servers are restarted after
            write.

        Use Cases:
            (1) Running IOR with different set of transfer size where first
            transfer size is < 4096 and then > 4096. Verify the data after
            servers are restarted.
            (2) Repeat the case(1) with maximum nvme pool size that can be
            created.
            (3) Running IOR with different set of transfer size where the
            transfer size is > 4096 throughout. Verify the data after
            servers are restarted.
            (4) Repeat the case(3) with maximum nvme pool size that can be
            created.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=nvme,daosio
        :avocado: tags=NvmeIoVerification,test_nvme_server_restart
        """
        ior_processes = self.params.get("np", '/run/ior/*')
        ior_transfer_size = self.params.get("tsize", '/run/ior/transfersize/*/')
        ior_block_size = self.ior_cmd.block_size.value
        ior_seq_pool_qty = self.params.get("ior_sequence_pool_qty", '/run/pool/*')
        ior_flag_write = self.params.get("write", '/run/ior/*/')
        ior_flag_read = self.params.get("read", '/run/ior/*/')
        job_manager = self.get_ior_job_manager_command()

        # Loop for every pool size
        for index in range(ior_seq_pool_qty):
            # Create and connect to a pool with namespace
            self.add_pool(namespace="/run/pool/pool_{}/*".format(index))

            # get pool info
            self.pool.get_info()

            for tsize in ior_transfer_size:
                # Run ior with the parameters specified for this pass
                self.ior_cmd.transfer_size.update(tsize)
                self.ior_cmd.flags.update(ior_flag_write)
                # if transfer size is less thank 1K
                # update block size to 32K to keep it small
                if tsize <= 1000:
                    self.ior_cmd.block_size.update(32000)
                else:
                    self.ior_cmd.block_size.update(ior_block_size)
                container = self.get_container(self.pool)
                self.ior_cmd.set_daos_params(self.pool, container.identifier)
                self.run_ior(job_manager, ior_processes)

                # Stop all servers
                self.get_dmg_command().system_stop(True)

                # Start all servers
                self.get_dmg_command().system_start()

                # check if all servers started as expected
                scan_info = self.get_dmg_command().system_query()
                if not check_system_query_status(scan_info):
                    self.fail("One or more servers crashed")

                # read all the data written before server restart
                self.ior_cmd.flags.update(ior_flag_read)
                self.run_ior(job_manager, ior_processes)

                # destroy the container
                container.destroy()

            # destroy pool
            self.pool.destroy()

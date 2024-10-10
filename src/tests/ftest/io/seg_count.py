"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase
from write_host_file import write_host_file


class SegCount(IorTestBase):
    """Test class Description: Runs IOR with different segment counts.

    :avocado: recursive
    """

    def test_segcount(self):
        """JIRA ID: DAOS-1782.

        Test Description:
            Run IOR with 32,64 and 128 clients with different segment counts.

        Use Cases:
            Different combinations of 32/64/128 Clients, 8b/1k/4k record size,
            1k/4k/1m/8m transfersize and stripesize and 16 async io.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=mpiio,ior
        :avocado: tags=SegCount,test_segcount
        """
        # Update the hostfile with the requested number of slots per host
        self.hostfile_clients = write_host_file(
            self.hostlist_clients, self.workdir, self.processes)

        # Set the IOR segment count
        if self.ior_cmd.block_size.value == '4k' and self.processes == 16:
            self.ior_cmd.segment_count.update(491500)
        elif self.ior_cmd.block_size.value == '4k' and self.processes == 32:
            self.ior_cmd.segment_count.update(245750)
        elif self.ior_cmd.block_size.value == '4k' and self.processes == 64:
            self.ior_cmd.segment_count.update(122875)
        elif self.ior_cmd.block_size.value == '1m' and self.processes == 16:
            self.ior_cmd.segment_count.update(1920)
        elif self.ior_cmd.block_size.value == '1m' and self.processes == 32:
            self.ior_cmd.segment_count.update(960)
        elif self.ior_cmd.block_size.value == '1m' and self.processes == 64:
            self.ior_cmd.segment_count.update(480)
        elif self.ior_cmd.block_size.value == '4m' and self.processes == 16:
            self.ior_cmd.segment_count.update(480)
        elif self.ior_cmd.block_size.value == '4m' and self.processes == 32:
            self.ior_cmd.segment_count.update(240)
        elif self.ior_cmd.block_size.value == '4m' and self.processes == 64:
            self.ior_cmd.segment_count.update(120)

        # Create a pool and run IOR
        self.run_ior_with_pool()

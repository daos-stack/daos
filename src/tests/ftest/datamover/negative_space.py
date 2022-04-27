#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from os.path import join


class DmvrNegativeSpaceTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for POSIX DataMover negative testing space errors.

    Test Class Description:
        Tests the following cases:
            Destination pool out of space.
            Destination POSIX out of space.
    :avocado: recursive
    """

    # DCP error codes
    MFU_ERR_DCP_COPY = "MFU_ERR(-1101)"

    def test_dm_negative_space_dcp(self):
        """
        Test Description:
            DAOS-5515: destination pool does not have enough space.
            DAOS-6387: posix filesystem does not have enough space.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=datamover,mfu,mfu_dcp,dfs,ior
        :avocado: tags=dm_negative,dm_negative_space_dcp
        """
        self.set_tool("DCP")

        # Create the source file
        src_posix_path = self.new_posix_test_path()
        src_posix_file = join(src_posix_path, self.ior_cmd.test_file.value)
        self.run_ior_with_params("POSIX", src_posix_file)

        # Create destination test pool and container
        dst_pool = self.create_pool()
        dst_cont = self.create_cont(dst_pool)
        dst_daos_path = "/"

        # Try to copy, and expect a proper error message.
        self.run_datamover(
            self.test_id + " (dst pool out of space)",
            "POSIX", src_posix_path, None, None,
            "DAOS_UUID", dst_daos_path, dst_pool, dst_cont,
            expected_rc=1,
            expected_output=[self.MFU_ERR_DCP_COPY, "errno=28"])

        # Mount small tmpfs filesystem on posix path.
        dst_posix_path = self.new_posix_test_path(mount_dir=True)

        # Try to copy. For now, we expect this to just abort.
        self.run_datamover(
            self.test_id + " (dst posix out of space)",
            "POSIX", src_posix_path, None, None,
            "POSIX", dst_posix_path,
            expected_rc=255,
            expected_err=["errno=28"])

'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from duns_utils import format_path


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
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp,dfs,ior
        :avocado: tags=DmvrNegativeSpaceTest,test_dm_negative_space_dcp
        """
        self.set_tool("DCP")

        self.log_step("Create source file in DAOS pool")
        src_pool = self.get_pool(connect=False, namespace="/run/pool_large/*")
        src_cont = self.get_container(src_pool)
        self.run_ior_with_params("DAOS", self.ior_cmd.test_file.value, src_pool, src_cont)

        self.log_step("Create small destination pool and container")
        dst_pool = self.get_pool(connect=False, namespace="/run/pool_small/*")
        dst_cont = self.get_container(dst_pool)

        self.log_step("Verify out of space error on destination pool")
        self.run_datamover(
            self.test_id + " (dst pool out of space)",
            src_path=format_path(src_pool, src_cont),
            dst_path=format_path(dst_pool, dst_cont),
            expected_rc=1,
            expected_output=[self.MFU_ERR_DCP_COPY, "errno=28"])

        self.log_step("Verify out of space error on POSIX destination")
        # Mount small tmpfs filesystem on posix path.
        dst_posix_path = self.new_posix_test_path(mount_dir_size=dst_pool.size.value)

        # Try to copy. For now, we expect this to just abort.
        self.run_datamover(
            self.test_id + " (dst posix out of space)",
            src_path=format_path(src_pool, src_cont),
            dst_path=dst_posix_path,
            expected_rc=255,
            expected_err=["errno=28"])

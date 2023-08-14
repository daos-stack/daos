"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from ior_test_base import IorTestBase


class IorHardBasic(IorTestBase):
    # pylint: disable=too-few-public-methods
    """Test class Description: Runs IOR Hard with different
                               EC OBject types.

    :avocado: recursive
    """

    def test_ior_hard(self):
        """Jira ID: DAOS-7313.

        Test Description:
            Run IOR Hard with EC Object types.

        Use Cases:
            Create the pool, container and run IOR Hard with EC Objects.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_array
        :avocado: tags=IorHardBasic,ec_ior,ior_hard,test_ior_hard
        """
        ior_read_flags = self.params.get("read_flags", "/run/ior/*")
        self.run_ior_with_pool()
        self.ior_cmd.flags.update(ior_read_flags)
        self.ior_cmd.sw_wearout.update(None)
        self.run_ior_with_pool(create_cont=False)

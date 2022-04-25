#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from mdtest_test_base import MdtestBase


class EcodMdtest(MdtestBase):
    # pylint: disable=too-many-ancestors
    """EC MDtest class to run smoke tests.

    Test Class Description: To validate Erasure code object type classes.

    :avocado: recursive
    """

    def test_ec_mdtest_smoke(self):
        """Jira ID: DAOS-2494.

        Test Description:
            Test EC object class with.
        Use Cases:
            Create the pool and run EC object class till 8P2.

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,large
        :avocado: tags=ec,ec_smoke,mdtest
        :avocado: tags=ec_mdtest_smoke
        """
        mdtest_flags = self.params.get("flags", "/run/mdtest/*")
        self.mdtest_cmd.flags.update(mdtest_flags)

        obj_class = self.params.get("dfs_oclass", '/run/mdtest/objectclass/*')
        for oclass in obj_class:
            self.mdtest_cmd.dfs_oclass.update(oclass)
            self.mdtest_cmd.dfs_dir_oclass.update(oclass)
            self.execute_mdtest()

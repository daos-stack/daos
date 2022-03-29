#!/usr/bin/python
'''
  (C) Copyright 2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from ior_test_base import IorTestBase
from fio_test_base import FioBase

class EcodFaultInjection(IorTestBase, FioBase):
    # pylint: disable=too-many-ancestors
    """EC Fault domains Test class.

    Test Class Description: To validate Erasure code object type classes with Fault injection.

    :avocado: recursive
    """

    def test_ec_ior_fault(self):
        """Jira ID: DAOS-7344.

        Test Description:
            Test Erasure code object with IOR when different Fault is getting injected.
        Use Case:
            Run IOR with supported EC object type. During test different fault is getting
            injected by test framework. Verify the IO works fine and there is no data corruption.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_ior_fault,faults
        :avocado: tags=ec_fault
        """
        obj_class = self.params.get("dfs_oclass", '/run/ior/objectclass/*')

        for oclass in obj_class:
            self.ior_cmd.dfs_oclass.update(oclass)
            self.ior_cmd.dfs_dir_oclass.update(oclass)
            self.run_ior_with_pool()

    def test_ec_fio_fault(self):
        """Jira ID: DAOS-7344.

        Test Description:
            Test Erasure code object with Fio when different Fault is getting injected.
        Use Case:
            Run Fio with supported EC RF. During test different fault is getting
            injected by test framework. Verify the fio works fine and there is no data
            corruption.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large,ib2
        :avocado: tags=ec,ec_array,ec_fio_fault,faults
        :avocado: tags=ec_fault
        """
        self.execute_fio()

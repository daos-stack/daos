#!/usr/bin/python3
'''
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from frontera_test_base import FronteraTestBase


class MdtestHard(FronteraTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs MdTest Hard.

    :avocado: recursive
    """

    def test_frontera_mdtest_hard(self):
        """

        Test Description:
            Run MdTest Hard.

        Use Cases:
            Create a pool, container, and run MdTest Hrad.

        :avocado: tags=frontera,manual
        :avocado: tags=vm
        :avocado: tags=frontera_mdtest_hard
        """
        self.run_performance_mdtest()
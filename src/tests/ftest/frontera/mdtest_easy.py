#!/usr/bin/python3
'''
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from frontera_test_base import FronteraTestBase


class MdtestEasy(FronteraTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs MdTest Easy.

    :avocado: recursive
    """

    def test_frontera_mdtest_easy(self):
        """

        Test Description:
            Run MdTest Easy.

        Use Cases:
            Create a pool, container, and run MdTest Easy.

        :avocado: tags=frontera,manual
        :avocado: tags=vm
        :avocado: tags=frontera_mdtest_easy
        """
        self.run_performance_mdtest()
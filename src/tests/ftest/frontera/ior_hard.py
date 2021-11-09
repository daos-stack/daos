#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from frontera_test_base import FronteraTestBase

class IorHard(FronteraTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """Test class Description: Run IOR Hard

    :avocado: recursive
    """

    def test_frontera_ior_hard(self):
        """

        Test Description:
            Run IOR Hard

        Use Cases:
            Create a pool, container, and run IOR Hard

        :avocado: tags=frontera,manual
        :avocado: tags=vm
        :avocado: tags=frontera_ior_hard
        """
        self.run_performance_ior()
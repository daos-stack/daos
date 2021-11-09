#!/usr/bin/python3
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from frontera_test_base import FronteraTestBase

class IorEasy(FronteraTestBase):
    # pylint: disable=too-many-ancestors
    # pylint: disable=too-few-public-methods
    """Test class Description: Run IOR Easy

    :avocado: recursive
    """

    def test_frontera_ior_easy(self):
        """

        Test Description:
            Run IOR Easy

        Use Cases:
            Create a pool, container, and run IOR Easy

        :avocado: tags=frontera,manual
        :avocado: tags=vm
        :avocado: tags=frontera_ior_easy
        """
        self.run_performance_ior()
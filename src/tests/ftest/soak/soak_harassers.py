#!/usr/bin/python
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from soak_test_base import SoakTestBase


class SoakHarassers(SoakTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs soak with harassers.

    :avocado: recursive
    """

    def test_soak_harassers(self):
        """Run all soak tests with harassers.

        Test ID: DAOS-2511
        Test Description: This will create a soak job that runs
        various harassers  defined in the soak yaml
        This test will run for the time specififed in
        /run/test_timeout.

        :avocado: tags=soak,soak_harassers
        """
        test_param = "/run/soak_harassers/"
        self.run_soak(test_param)

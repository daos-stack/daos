"""
(C) Copyright 2018-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from soak_test_base import SoakTestBase


class SoakHarassers(SoakTestBase):
    """Test class Description: Runs soak with harassers.

    :avocado: recursive
    """

    def test_soak_offline_harassers(self):
        """Run all soak tests with harassers.

        Test ID: DAOS-2511
        Test Description: This will create a soak job that runs
        various offline harassers  defined in the soak yaml
        This test will run for the time specified in
        /run/test_timeout.

        :avocado: tags=manual
        :avocado: tags=hw,24
        :avocado: tags=soak,soak_harassers
        :avocado: tags=SoakHarassers,test_soak_offline_harassers
        """
        test_param = "/run/soak_harassers/"
        self.run_soak(test_param)

    def test_soak_online_harassers(self):
        """Run all soak tests with harassers.

        Test ID: DAOS-2511
        Test Description: This will create a soak job that runs
        various online harassers  defined in the soak yaml
        This test will run for the time specified in
        /run/test_timeout.

        :avocado: tags=manual
        :avocado: tags=hw,24
        :avocado: tags=soak,soak_harassers
        :avocado: tags=SoakHarassers,test_soak_online_harassers
        """
        test_param = "/run/soak_harassers/"
        self.run_soak(test_param)

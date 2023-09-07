"""
(C) Copyright 2018-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from soak_test_base import SoakTestBase


class SoakStressTwoHour(SoakTestBase):
    """Test class Description: Runs soak smoke.

    :avocado: recursive
    """

    def test_soak_stress(self):
        """Run all soak tests .

        Test ID: DAOS-2256
        Test ID: DAOS-2509
        Test Description: This will create a slurm batch job that runs
        various jobs defined in the soak yaml
        This test will run soak_stress for 2 hours.

        :avocado: tags=manual
        :avocado: tags=hw,large
        :avocado: tags=soak
        :avocado: tags=SoakStressTwoHour,soak_stress_2h,test_soak_stress
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)

"""
(C) Copyright 2018-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from soak_test_base import SoakTestBase


class SoakStress(SoakTestBase):
    """Test class Description: Runs soak smoke.

    :avocado: recursive
    """

    def test_soak_stress_72h(self):
        """Run soak test for 72hours on performance cluster.

        Test Description: This will create a slurm batch jobs that run
        various jobs defined in the soak yaml
        This test will run soak_stress for 72 hours.

        :avocado: tags=manual
        :avocado: tags=hw,24
        :avocado: tags=soak,soak_stress
        :avocado: tags=SoakStress,test_soak_stress_72h
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)

    def test_soak_stress_48h(self):
        """Run soak test for 48hours on performance cluster.

        Test Description: This will create a slurm batch jobs that run
        various jobs defined in the soak yaml
        This test will run soak_stress for 48 hours.

        :avocado: tags=manual
        :avocado: tags=hw,24
        :avocado: tags=soak,soak_stress
        :avocado: tags=SoakStress,test_soak_stress_48h
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)

    def test_soak_stress_24h(self):
        """Run soak test for 24hours on performance cluster.

        Test Description: This will create a slurm batch jobs that run
        various jobs defined in the soak yaml
        This test will run soak_stress for 24 hours.

        :avocado: tags=manual
        :avocado: tags=hw,24
        :avocado: tags=soak,soak_stress
        :avocado: tags=SoakStress,test_soak_stress_24h
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)

    def test_soak_stress_12h(self):
        """Run soak test for 12hours on performance cluster.

        Test Description: This will create a slurm batch jobs that run
        various jobs defined in the soak yaml
        This test will run soak_stress for 12 hours.

        :avocado: tags=manual
        :avocado: tags=hw,24
        :avocado: tags=soak,soak_stress
        :avocado: tags=SoakStress,test_soak_stress_12h
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)

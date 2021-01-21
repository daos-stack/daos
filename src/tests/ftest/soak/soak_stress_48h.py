#!/usr/bin/python
"""
(C) Copyright 2018-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from soak_test_base import SoakTestBase


class SoakStress(SoakTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Runs soak smoke.

    :avocado: recursive
    """

    def test_soak_stress(self):
        """Run soak test for 48hours on performance cluster.

        Test Description: This will create a slurm batch jobs that run
        various jobs defined in the soak yaml
        This test will run soak_stress for 48 hours.

        :avocado: tags=soak,soak_stress_48h
        """
        test_param = "/run/soak_stress/"
        self.run_soak(test_param)

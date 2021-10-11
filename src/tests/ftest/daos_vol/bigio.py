#!/usr/bin/python
"""
(C) Copyright 2019-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from vol_test_base import VolTestBase
from general_utils import get_job_manager_class


class DaosVol(VolTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Runs HDF5 test suites with daos vol connector.

    :avocado: recursive
    """

    def test_daos_vol_bigio(self):
        """Jira ID: DAOS-3656.

        Test Description:
            Run HDF5 h5_partest_t_bigio provided in HDF5 package with
            daos vol connector and mpich. Testing various I/O functions
            provided in HDF5 test suite such as:

              h5_partest_t_bigio


        :avocado: tags=all,full_regression
        :avocado: tags=hw,small
        :avocado: tags=hdf5,vol,volbigio
        :avocado: tags=DAOS_5610
        """
        self.job_manager = get_job_manager_class("Mpirun", None, False, "mpich")
        self.set_job_manager_timeout()
        self.run_test(
            "/usr/lib64/mpich/lib", "/usr/lib64/hdf5_vol_daos/mpich/tests")

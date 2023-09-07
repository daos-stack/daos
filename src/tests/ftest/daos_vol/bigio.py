"""
(C) Copyright 2019-2022 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from vol_test_base import VolTestBase
from job_manager_utils import get_job_manager


class DaosVolBigIO(VolTestBase):
    # pylint: disable=too-few-public-methods
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
        :avocado: tags=hw,medium
        :avocado: tags=hdf5,daos_vol,vol
        :avocado: tags=DaosVolBigIO,test_daos_vol_bigio
        """
        manager = get_job_manager(self, mpi_type="mpich")
        self.run_test(manager, "/usr/lib64/mpich/lib", "/usr/lib64/hdf5_vol_daos/mpich/tests")

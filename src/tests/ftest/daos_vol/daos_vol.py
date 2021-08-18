#!/usr/bin/python
"""
(C) Copyright 2019-2021 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from vol_test_base import VolTestBase
from general_utils import get_job_manager_class
from apricot import skipForTicket

class DaosVol(VolTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Runs HDF5 test suites with daos vol connector.

    :avocado: recursive
    """

    @skipForTicket("DAOS-8207")
    def test_daos_vol_mpich(self):
        """Jira ID: DAOS-3656.

        Test Description:
            Run HDF5 testphdf5 and t_shapesame provided in HDF5 package with
            daos vol connector. Testing various I/O functions provided in HDF5
            test suite such as:
              h5_test_testhdf5
              h5vl_test
              h5_partest_t_bigio
              h5_partest_testphdf5
              h5vl_test_parallel
              h5_partest_t_shapesame
              h5daos_test_map
              h5daos_test_map_parallel
              h5daos_test_oclass
              h5daos_test_metadata_parallel

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw
        :avocado: tags=hdf5,vol,volunit,volmpich
        :avocado: tags=DAOS_5610
        """
        self.job_manager = get_job_manager_class("Mpirun", None, False, "mpich")
        self.set_job_manager_timeout()
        self.run_test(
            "/usr/lib64/mpich/lib", "/usr/lib64/hdf5_vol_daos/mpich/tests")

    @skipForTicket("DAOS-8207")
    def test_daos_vol_openmpi(self):
        """Jira ID: DAOS-3656.

        Test Description:
            Run HDF5 testphdf5 and t_shapesame provided in HDF5 package with
            daos vol connector. Testing various I/O functions provided in HDF5
            test suite such as:
              h5_test_testhdf5
              h5vl_test
              h5_partest_t_bigio
              h5_partest_testphdf5
              h5vl_test_parallel
              h5_partest_t_shapesame
              h5daos_test_map
              h5daos_test_map_parallel
              h5daos_test_oclass
              h5daos_test_metadata_parallel

        :avocado: tags=all,daily_regression
        :avocado: tags=hw
        :avocado: tags=hdf5,vol,volunit,volopenmpi
        :avocado: tags=DAOS_5610
        """
        self.job_manager = get_job_manager_class(
            "Orterun", None, False, "openmpi")
        self.set_job_manager_timeout()
        self.run_test(
            "/usr/lib64/openmpi3/lib",
            "/usr/lib64/hdf5_vol_daos/openmpi3/tests")

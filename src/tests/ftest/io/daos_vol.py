#!/usr/bin/python
"""
(C) Copyright 2019 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from vol_test_base import VolTestBase


# pylint: disable=too-many-ancestors
class DaosVol(VolTestBase):
    """Runs HDF5 test suites with daos vol connector.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Cancel h5_test_testhdf5 with MPICH
        mpi_type = self.params.get("job_manager_mpi_type")
        testname = self.params.get("testname")
        if mpi_type == "mpich" and testname == "h5_test_testhdf5":
            self.cancelForTicket("DAOS-5469")
        if testname == "h5_partest_t_bigio":
            self.cancelForTicket("DAOS-5496")
        super(DaosVol, self).setUp()

    def test_daos_vol(self):
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

        :avocado: tags=all,hw,small,hdf5,vol,DAOS_5610
        """
        self.run_test()

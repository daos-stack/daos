#!/usr/bin/python
"""
(C) Copyright 2019-2020 Intel Corporation.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
The Government's rights to use, modify, reproduce, release, perform, display,
or disclose this software are subject to the terms of the Apache License as
provided in Contract No. B609815.
Any reproduction of computer software, computer software documentation, or
portions thereof marked with this legend must also reproduce the markings.
"""
from vol_test_base import VolTestBase
from general_utils import get_job_manager_class


class DaosVol(VolTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Runs HDF5 test suites with daos vol connector.

    :avocado: recursive
    """

    # Test variants that should be skipped
    CANCEL_FOR_TICKET = [
        ["DAOS-5831", "testname", "h5_partest_t_shapesame"],
        ["DAOS-5469", "testname", "h5_test_testhdf5"],
        ["DAOS-6076", "testname", "h5_partest_testphdf5"],
        ["DAOS-5496", "testname", "h5_partest_t_bigio"],
        ["DAOS-5647", "testname", "h5vl_test_parallel"],
    ]

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

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=hdf5,vol
        :avocado: tags=DAOS_5610
        """
        self.job_manager = get_job_manager_class("Mpirun", None, False, "mpich")
        self.set_job_manager_timeout()
        self.run_test(
            "/usr/lib64/mpich/lib", "/usr/lib64/hdf5_vol_daos/mpich/tests")

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

        :avocado: tags=all,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=hdf5,vol
        :avocado: tags=DAOS_5610
        """
        self.job_manager = get_job_manager_class(
            "Orterun", None, False, "openmpi")
        self.set_job_manager_timeout()
        self.run_test(
            "/usr/lib64/openmpi3/lib",
            "/usr/lib64/hdf5_vol_daos/openmpi3/tests")

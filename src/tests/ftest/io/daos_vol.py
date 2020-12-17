#!/usr/bin/python
"""
(C) Copyright 2019 Intel Corporation.

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


class DaosVol(VolTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Runs HDF5 test suites with daos vol connector.

    :avocado: recursive
    """

    # Test variants that should be skipped
    CANCEL_FOR_TICKET = (
        ("DAOS-5831", "testname", "h5_partest_t_shapesame"),
        ("DAOS-5469", "testname", "h5_test_testhdf5"),
        ("DAOS-6076", "testname", "h5_partest_testphdf5"),
        ("DAOS-5496", "testname", "h5_partest_t_bigio"),
        ("DAOS-5647", "testname", "h5vl_test_parallel"),
    )

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

        :avocado: tags=all,pr,hw,small,hdf5,vol,DAOS_5610
        """
        self.run_test()

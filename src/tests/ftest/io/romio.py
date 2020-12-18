#!/usr/bin/python
'''
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
'''
from __future__    import print_function
from mpio_test_base import MpiioTests

# pylint: disable=too-many-ancestors
class Romio(MpiioTests):
    """
    Runs Romio test.
    :avocado: recursive
    """

    def test_romio(self):
        """
        Test ID: DAOS-1994
        Run Romio test provided in mpich package
        Testing various I/O functions provided in romio test suite
        :avocado: tags=all,mpiio,pr,daily_regression,small,romio
        """
        # setting romio parameters
        test_repo = self.params.get("romio_repo", '/run/romio/')
        self.run_test(test_repo, "romio")

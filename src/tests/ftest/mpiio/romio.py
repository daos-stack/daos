#!/usr/bin/python3
'''
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from mpio_test_base import MpiioTests
from job_manager_utils import get_job_manager


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
        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,small
        :avocado: tags=mpiio,mpich,romio
        """
        # setting romio parameters
        test_repo = self.params.get("romio_repo", '/run/romio/')
        manager = get_job_manager(self, mpi_type="mpich")
        self.run_test(manager, test_repo, "romio")

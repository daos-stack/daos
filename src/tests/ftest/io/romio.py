#!/usr/bin/python
'''
  (C) Copyright 2019-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
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

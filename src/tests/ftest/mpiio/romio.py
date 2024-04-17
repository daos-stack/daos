'''
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

from mpiio_test_base import MpiioTests


class Romio(MpiioTests):
    """Run the Romio test.

    :avocado: recursive
    """

    def test_romio(self):
        """Test ID: DAOS-1994.

        Run Romio test provided in mpich package
        Testing various I/O functions provided in romio test suite

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=mpiio,mpich,romio
        :avocado: tags=Romio,test_romio
        :avocado: tags=master_failed
        """
        # setting romio parameters
        test_repo = self.params.get("romio_repo", '/run/romio/')
        self.run_test(test_repo, "romio")

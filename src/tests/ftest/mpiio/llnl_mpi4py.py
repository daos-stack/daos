"""
  (C) Copyright 2019-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from mpiio_test_base import MpiioTests


class LlnlMpi4py(MpiioTests):
    """Runs LLNL and MPI4PY test suites.

    :avocado: recursive
    """

    def test_llnl(self):
        """Jira ID: DAOS-2231

        Test Description: Run LLNL test suite.
        Testing various I/O functions provided in llnl test suite
        such as:-
        test_collective, test_datareps, test_errhandlers,
        test_filecontrol, test_localpointer, test_manycomms,
        test_manyopens, test_openclose, test_openmodes,
        test_nb_collective, test_nb_localpointer, test_nb_rdwr,
        test_nb_readwrite, test_rdwr, test_readwrite

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=vm
        :avocado: tags=mpiio,smoke,mpich,llnl
        :avocado: tags=LlnlMpi4py,test_llnl
        """
        test_repo = self.params.get("llnl", '/run/test_repo/')
        self.run_test(test_repo, "llnl")

    def test_mpi4py(self):
        """Jira ID: DAOS-2231

        Test Description: Run mpi4py io test provided in mpi4py package
        Testing various I/O functions provided in mpi4py test suite
        such as:-
        testReadWriteAt, testIReadIWriteAt, testReadWrite
        testIReadIWrite, testReadWriteAtAll, testIReadIWriteAtAll
        testReadWriteAtAllBeginEnd, testReadWriteAll
        testIReadIWriteAll, testReadWriteAllBeginEnd

        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=mpiio,mpich,mpi4py
        :avocado: tags=LlnlMpi4py,test_mpi4py
        """
        test_repo = self.params.get("mpi4py", '/run/test_repo/')
        self.run_test(test_repo, "mpi4py")

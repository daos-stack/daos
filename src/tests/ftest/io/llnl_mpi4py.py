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
from __future__    import print_function
from mpio_test_base import MpiioTests


# pylint: disable=too-many-ancestors
class LlnlMpi4py(MpiioTests):
    """
    Runs LLNL and MPI4PY test suites.
    :avocado: recursive
    """

    def test_llnl(self):
        """
        Jira ID: DAOS-2231
        Test Description: Run LLNL test suite.
        Testing various I/O functions provided in llnl test suite
        such as:-
        test_collective, test_datareps, test_errhandlers,
        test_filecontrol, test_localpointer, test_manycomms,
        test_manyopens, test_openclose, test_openmodes,
        test_nb_collective, test_nb_localpointer, test_nb_rdwr,
        test_nb_readwrite, test_rdwr, test_readwrite

        :avocado: tags=all,mpiio,smoke,pr,daily_regression,small,llnl
        """
        test_repo = self.params.get("llnl", '/run/test_repo/')
        self.run_test(test_repo, "llnl")

    def test_mpi4py(self):
        """
        Jira ID: DAOS-2231
        Test Description: Run mpi4py io test provided in mpi4py package
        Testing various I/O functions provided in mpi4py test suite
        such as:-
        testReadWriteAt, testIReadIWriteAt, testReadWrite
        testIReadIWrite, testReadWriteAtAll, testIReadIWriteAtAll
        testReadWriteAtAllBeginEnd, testReadWriteAll
        testIReadIWriteAll, testReadWriteAllBeginEnd

        :avocado: tags=all,mpiio,daily_regression,small,mpi4py
        """
        test_repo = self.params.get("mpi4py", '/run/test_repo/')
        self.run_test(test_repo, "mpi4py")

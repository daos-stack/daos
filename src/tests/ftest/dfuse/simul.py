"""
  (C) Copyright 2018-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

import os

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from host_utils import get_local_host
from job_manager_utils import Mpirun


class PosixSimul(TestWithServers):
    """Tests a posix container with simul.

    From : https://github.com/LLNL/simul
    "simul" is an MPI coordinated test of parallel filesystem system calls and
    library functions.  It was designed to perform filesystem operations
    simultaneously from many nodes and processes to test the correctness
    and coherence of parallel file systems.

    List of tests:
        Test #0: open, shared mode.
        Test #1: close, shared mode.
        Test #2: file stat, shared mode.
        Test #3: lseek, shared mode.
        Test #4: read, shared mode.
        Test #5: write, shared mode.
        Test #6: chdir, shared mode.
        Test #7: directory stat, shared mode.
        Test #8: statfs, shared mode.
        Test #9: readdir, shared mode.
        Test #10: mkdir, shared mode.
        Test #11: rmdir, shared mode.
        Test #12: unlink, shared mode.
        Test #13: rename, shared mode.
        Test #14: creat, shared mode.
        Test #15: truncate, shared mode.
        Test #16: symlink, shared mode.
        Test #17: readlink, shared mode.
        Test #18: link to one file, shared mode.
        Test #19: link to a file per process, shared mode.
        Test #20: fcntl locking, shared mode.
        Test #21: open, individual mode.
        Test #22: close, individual mode.
        Test #23: file stat, individual mode.
        Test #24: lseek, individual mode.
        Test #25: read, individual mode.
        Test #26: write, individual mode.
        Test #27: chdir, individual mode.
        Test #28: directory stat, individual mode.
        Test #29: statfs, individual mode.
        Test #30: readdir, individual mode.
        Test #31: mkdir, individual mode.
        Test #32: rmdir, individual mode.
        Test #33: unlink, individual mode.
        Test #34: rename, individual mode.
        Test #35: creat, individual mode.
        Test #36: truncate, individual mode.
        Test #37: symlink, individual mode.
        Test #38: readlink, individual mode.
        Test #39: link to one file, individual mode.
        Test #40: link to a file per process, individual mode.
        Test #41: fcntl locking, individual mode.

    :avocado: recursive
    """

    def run_simul(self, exclude=None, faillist=None):
        """Run simul.

        If an include value is set, the exclude value is ignored and vice versa.

        Args:
            exclude (str, optional): comma-separated list of tests to exclude. Defaults to None.
            faillist (str array, optional): The list of tests expected to fail. Defaults to None.

        """
        if faillist is None:
            # run tests expect to pass
            raise_exception = True
            if exclude is None:
                self.fail("Both exclude and faillist are None")
        else:
            # run tests expect to fail
            raise_exception = False
        mpi_type = self.params.get("mpi_type", "/run/mpi/*", "")
        simul_path = self.params.get("simul_path", "/run/mpi/*", "")
        ppn = self.params.get("ppn", "/run/client_processes/*", "")

        # Create a pool
        self.log_step("Create a pool")
        pool = self.get_pool()

        # Create a container
        self.log_step("Create container")
        container = self.get_container(pool)

        # Setup dfuse
        self.log_step("Start dfuse")
        dfuse = get_dfuse(self, self.agent_managers[0].hosts)
        start_dfuse(self, dfuse, pool, container)
        dfuse.check_running()

        simul_cmd = os.path.join(simul_path, "simul")
        self.log_step(f"Running simul on {mpi_type}")
        if faillist is None:
            mpirun = Mpirun(f"{simul_cmd} -vv -d {dfuse.mount_dir.value} -e {exclude}",
                            mpi_type=mpi_type)
            mpirun.assign_hosts(get_local_host(), dfuse.mount_dir.value)
            mpirun.assign_processes(ppn=ppn)
            mpirun.run(raise_exception=raise_exception)
        else:
            for test_to_fail in faillist:
                mpirun = Mpirun(f"{simul_cmd} -vv -d {dfuse.mount_dir.value} -i {test_to_fail}",
                                mpi_type=mpi_type)
                mpirun.assign_hosts(get_local_host(), dfuse.mount_dir.value)
                mpirun.assign_processes(ppn=ppn)
                out = mpirun.run(raise_exception=raise_exception)
                # testing cases that are expected to fail
                if "FAILED in simul" in out.stdout_text:
                    self.log.info("Test %s failed as expected", test_to_fail)
                else:
                    self.fail(f"Test {test_to_fail} was expected to fail, but passed")

    def test_posix_simul(self):
        """Test simul.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=posix,simul,dfuse
        :avocado: tags=PosixSimul,test_posix_simul
        """
        # test  9, readdir, shared mode, dfuse returns NULL for readdir of an empty dir
        # test 18, link, shared mode, daos does not support hard link
        # test 20, fcntl locking, shared mode, daos does not support flock
        # test 30, readdir, individual mode, dfuse returns NULL for readdir of an empty dir
        # test 39, link, individual mode, daos does not support hard link
        # test 40, link, individual mode, daos does not support hard link
        # test 41, fcntl locking, individual mode, daos does not support flock
        self.run_simul(exclude="9,18,20,30,39,40,41")
        self.log.info('Test passed')

    def test_posix_expected_failures(self):
        """Test simul, expected failures.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=posix,simul,dfuse
        :avocado: tags=PosixSimul,test_posix_expected_failures
        """
        faillist = {"9", "18", "20", "30", "39", "40", "41"}
        self.run_simul(faillist=faillist)
        self.log.info('Test passed')

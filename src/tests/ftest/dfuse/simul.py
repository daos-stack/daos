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

    def run_simul(self, include=None, exclude=None, raise_exception=True):
        """Run simul.

        If an include value is set, the exclude value is ignored and vice versa.

        Args:
            include (str, optional): comma-separated list of tests to include. Defaults to None.
            exclude (str, optional): comma-separated list of tests to exclude. Defaults to None.
            raise_exception (bool, optional): whether to raise an exception. Defaults to True.

        Raises:
            MPILoadError: if there is an error loading the MPI

        Returns:
            CmdResult: result from the simul command

        """
        mpi_type = self.params.get("mpi_type", "/run/*", "")
        simul_path = self.params.get("simul_path", "/run/*", "")
        num_proc = self.params.get("np", "/run/client_processes/*", "")

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
        if include and not exclude:
            cmd = f"{simul_cmd} -vv -d {dfuse.mount_dir.value} -i {include}"
        elif exclude and not include:
            cmd = f"{simul_cmd} -vv -d {dfuse.mount_dir.value} -e {exclude}"
        else:
            cmd = None  # appease pylint
            self.fail("##Both include and exclude tests are selected both or empty.")

        self.log_step(f"Running simul on {mpi_type}")
        mpirun = Mpirun(cmd, mpi_type=mpi_type)
        mpirun.assign_hosts(get_local_host(), dfuse.mount_dir.value)
        mpirun.assign_processes(num_proc)
        out = mpirun.run(raise_exception=raise_exception)
        if include:
            # testing cases that are expected to fail
            return out
        return None

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
        expected_failures = {"9": None, "18": None, "20": None, "30": None, "39": None, "40": None,
                             "41": None}
        for test in sorted(expected_failures):
            expected_failures[test] = self.run_simul(include=test, raise_exception=False)
        failed = []
        for test in sorted(expected_failures):
            if "FAILED in simul" in expected_failures[test].stdout_text:
                self.log.info("Test %s failed as expected", test)
            else:
                self.log.info("Test %s was expected to fail, but passed", test)
                failed.append(test)
        if failed:
            self.fail(f"Simul tests {', '.join(failed)} expected to failed, but passed")
        self.log.info('Test passed')

"""
  (C) Copyright 2021-2024 Intel Corporation.
  (C) Copyright 2025 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import random
import string

from apricot import TestWithServers
from dfuse_utils import get_dfuse, start_dfuse
from exception_utils import CommandFailure
from io_utilities import DirectoryTreeCommand
from profiler_utils import SimpleProfiler
from run_utils import run_remote


class DfuseFind(TestWithServers):
    """Base DfuseFind test class.

    :avocado: recursive
    """

    def test_dfuse_find(self):
        """Jira ID: DAOS-5563.

        Test Description:
            The purpose of this test is to create some number of POSIX
            containers using dfuse and link them together such that they form
            a tree with a common root directory. Then, run the find(1) command
            to locate specific files in various parts of the tree.

            The test will fail if find(1) command crashes or if the number of
            files found is not equal to the number of files created.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=daosio,dfuse
        :avocado: tags=DfuseFind,test_dfuse_find
        """
        self._test_findcmd()

    def test_dfuse_find_perf(self):
        """Jira ID: DAOS-5563.

        Test Description:
            The purpose of this test is to benchmark the 'findcmd' test on a
            given file system pointed by the 'challenger_path' parameter and
            compared it against DAOS.

            The test will fail if DAOS performance is lower than the
            challenger performance.

        :avocado: tags=all,manual
        :avocado: tags=DfuseFind,test_dfuse_find_perf
        """
        # Number of repetitions each test will run.
        samples = self.params.get("samples", '/run/perf/*')
        # Challenger file system path. Ex: /mnt/lustre
        challenger_path = self.params.get("challenger_path", '/run/perf/*')
        self._test_findcmd(samples, challenger_path)

    def _test_findcmd(self, samples=1, challenger_path=""):
        """
        Create some number of POSIX containers using dfuse and link them
        together such that they form a tree with a common root directory. Then,
        run the find(1) command to locate specific files in various parts of
        the tree. If the parameter challenger_path is provided, the test will
        run on it and compare the performance of the provided file system
        against DAOS File System. The test will run sample times and will
        maximum data points.

        Parameters:
            samples         (int): Number of repetitions each test will run.
            challenger_path (str): Path where the challenger file system is
                                   mounted. Example: /mnt/lustre
        """
        # Number of containers to be created and mounted.
        cont_count = self.params.get("cont_count", '/run/container/*')
        # Common root directory where the containers will be mounted.
        dfs_path = self.params.get("dfs_path", '/run/find_cmd/*')
        # Height of the directory-tree.
        height = self.params.get("height", '/run/find_cmd/*')
        # Number of sub directories per directories.
        subdirs_per_node = self.params.get("subdirs_per_node", '/run/find_cmd/*')
        # Number of files created per directory.
        files_per_node = self.params.get("files_per_node", '/run/find_cmd/*')
        # Number of *.needle files that will be created.
        needles = self.params.get("needles", '/run/find_cmd/*')
        temp_dfs_path = ""

        dfuse_list = []
        containers = []

        pool = self.get_pool(connect=False)

        if not dfs_path:
            temp_dfs_path = self._generate_temp_path_name("/tmp", "dfs_test_")
            dfs_path = temp_dfs_path

        if challenger_path:
            challenger_path = self._generate_temp_path_name(challenger_path, "test_")

        def _run_find_test(test_root, dir_tree_paths):
            self._create_dir_forest(
                dir_tree_paths, height, subdirs_per_node, files_per_node, needles)

            return self._run_find_test(test_root, samples, cont_count, needles)

        try:
            mount_dirs = self._setup_containers(pool, dfs_path, cont_count, dfuse_list, containers)
            daos_stats = _run_find_test(dfs_path, mount_dirs)

            if challenger_path:
                challenger_dirs = self._setup_challenger(challenger_path, cont_count)
                challenger_stats = _run_find_test(challenger_path, challenger_dirs)

        except CommandFailure as error:
            self.log.error("FindCmd Test Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
        except Exception as error:
            self.log.error("FindCmd Test Failed: %s", str(error))
            raise
        finally:
            for dfuse in dfuse_list:
                dfuse.stop()
            for container in containers:
                container.destroy()
            pool.destroy()

            if challenger_path:
                self._run_cmd("rm -rf {0}".format(challenger_path))

            if temp_dfs_path:
                self._run_cmd("rm -rf {0}".format(temp_dfs_path))

        self.log.info("DAOS Stats")
        daos_stats.print_stats()

        if challenger_path:
            self.log.info("Challenger Stats")
            challenger_stats.print_stats()

            self._compare_results(daos_stats, challenger_stats, "all_files")
            self._compare_results(daos_stats, challenger_stats, "same_suffix")
            self._compare_results(daos_stats, challenger_stats, "unique_file")

    def _compare_results(self, daos_stats, challenger_stats, tag):
        """
        Compare the stats of DAOS and the challenger. If the challenger is
        faster than DAOS, the test fails.
        """
        daos_max_time, _, _ = daos_stats.get_stat(tag)
        challenger_max_time, _, _ = challenger_stats.get_stat(tag)

        if daos_max_time >= challenger_max_time:
            self.log.error("Impossible, DAOS is slower running '%s' tag", tag)
            self.fail("Test was expected to pass but it failed")
        else:
            self.log.info("DAOS is equal or faster running '%s' tag", tag)

    def _run_find_test(self, test_path, samples, containers, needles):
        """
        Perform the actual test, run find(1) to locate the needle files in
        the directory forest. If the number of files located does not match
        the number of expected files. The test fails.
        """
        profiler = SimpleProfiler()
        profiler.set_logger(self.log)

        def _search_needles(file_name, sample_tag, expected_res):
            self.log.info("Searching pattern: %s", file_name)
            self.log.info("Number of expecting results: %d", expected_res)
            cmd = "find {} -name {} | wc -l | grep {}".format(test_path, file_name, expected_res)
            profiler.run(self._run_cmd, sample_tag, cmd)

        self.log.info("Sampling path: %s", test_path)

        for sample_num in range(1, samples + 1):
            self.log.info("Running sample number %d of %d", sample_num, samples)

            prefix = random.randrange(containers - 1)  # nosec
            suffix = random.randrange(needles - 1)  # nosec
            file_name = "t{:05d}_*_{:05d}.needle".format(prefix, suffix)
            _search_needles(file_name, "unique_file", 1)

            number = random.randrange(needles - 1)  # nosec
            file_name = "*_{:05d}.needle".format(number)
            _search_needles(file_name, "same_suffix", containers)

            _search_needles("*.needle", "all_files", containers * needles)

        return profiler

    def _setup_containers(self, pool, dfs_path, cont_count, dfuse_list, containers):
        """
        Setup as many containers as the test requested. Return the paths where
        the containers were mounted.
        """
        mount_dirs = []
        for count in range(cont_count):
            container = self.get_container(pool)
            cont_dir = "{}_daos_dfuse_{}".format(pool.uuid, count)
            mount_dir = os.path.join(dfs_path, cont_dir)
            self.log.info("Creating container Pool UUID: %s Con UUID: %s", pool, container)
            dfuse = get_dfuse(self, self.hostlist_clients)
            start_dfuse(self, dfuse, pool, container, mount_dir=mount_dir)
            dfuse_list.append(dfuse)
            containers.append(container)
            mount_dirs.append(dfuse.mount_dir.value)

        return mount_dirs

    def _create_dir_forest(self, paths, height, subdirs, files_per_node, needles):
        """Create a directory tree on each path listed in the paths variable"""
        count = 0
        for path in paths:
            dir_tree = DirectoryTreeCommand(self.hostlist_clients)
            dir_tree.path.value = path
            dir_tree.height.value = height
            dir_tree.subdirs.value = subdirs
            dir_tree.files.value = files_per_node
            dir_tree.needles.value = needles
            dir_tree.prefix.value = f"t{count:05d}_"
            count += 1

            self.log.info("Populating: %s", path)
            result = dir_tree.run()
            if not result.passed:
                self.fail(
                    f"Error running '{dir_tree.command}' for '{path}' on {result.failed_hosts}")

    def _run_cmd(self, cmd):
        """Run the command on the remote client hosts.

        Args:
            cmd (str): command to run
        """
        result = run_remote(self.log, self.hostlist_clients, cmd, timeout=180)
        if not result.passed:
            self.fail(f"Error running '{cmd}' on {result.failed_hosts}")

    def _setup_challenger(self, test_path, directories):
        """Create the paths where the directory trees of the challenger will be created."""
        challenger_dirs = []

        self.log.info("Challenger test root %s", test_path)

        for directory in range(directories):
            dir_name = "test_dir_{:05d}".format(directory)
            dir_name = os.path.join(test_path, dir_name)
            self._run_cmd("mkdir -p {0}".format(dir_name))
            challenger_dirs.append(dir_name)

        return challenger_dirs

    @classmethod
    def _generate_temp_path_name(cls, root, prefix):
        """
        Creates path that can be used to create temporary files or directories.
        The return value is concatenation of root and a random string prefixed
        with the prefix value.
        """
        letters = string.ascii_lowercase + string.digits
        random_name = "".join(random.choice(letters) for _ in range(8))  # nosec

        return os.path.join(root, "{}{}".format(prefix, random_name))

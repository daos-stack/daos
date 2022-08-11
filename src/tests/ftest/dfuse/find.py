"""
  (C) Copyright 2021-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import sys
import string
import random
from ClusterShell.NodeSet import NodeSet

import general_utils
from dfuse_test_base import DfuseTestBase
from dfuse_utils import get_dfuse, start_dfuse
from exception_utils import CommandFailure
from io_utilities import DirTree


class DfuseFind(DfuseTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
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
        :avocado: tags=dfuse_find,test_dfuse_find
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
        :avocado: tags=hw,medium
        :avocado: tags=dfuse_find,test_dfuse_find_perf
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

        dfuses = []
        containers = []

        self.add_pool(connect=False)

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
            mount_dirs = self._setup_containers(dfs_path, cont_count, dfuses, containers)

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
            self._teardown_dfuse(dfuses)
            self.destroy_containers(containers)
            self.pool.destroy()

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
        profiler = general_utils.SimpleProfiler()
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

    def _setup_containers(self, dfs_path, cont_count, dfuses, containers):
        """
        Setup as many containers as the test requested. Return the paths where
        the containers were mounted.
        """
        mount_dirs = []
        for count in range(cont_count):
            self.add_container(self.pool)
            cont_dir = "{}_daos_dfuse_{}".format(self.pool.uuid, count)
            mount_dir = os.path.join(dfs_path, cont_dir)
            self.log.info(
                "Creating container Pool UUID: %s Con UUID: %s", self.pool, self.container)
            dfuse = get_dfuse(self, self.hostlist_clients)
            start_dfuse(self, dfuse, pool=self.pool, container=self.container, mount_dir=mount_dir)
            dfuses.append(dfuse)
            containers.append(self.container)
            mount_dirs.append(dfuse.mount_dir.value)

        return mount_dirs

    def _create_dir_forest(self, paths, height, subdirs, files_per_node, needles):
        """Create a directory tree on each path listed in the paths variable"""

        remote_pythonpath = ":".join(sys.path)

        count = 0
        for path in paths:
            self.log.info("Populating: %s", path)
            prefix = "t{:05d}_".format(count)
            count += 1
            remote_args = "{} {} {} {} {} {}".format(
                path, height, subdirs, files_per_node, needles, prefix)
            dir_tree_cmd = "PYTHONPATH={} python3 {} {}".format(
                remote_pythonpath, os.path.abspath(__file__), remote_args)
            self._run_cmd(dir_tree_cmd)

    def _run_cmd(self, cmd):
        ret_code = general_utils.pcmd(self.hostlist_clients, cmd, timeout=180)
        if 0 not in ret_code:
            error_hosts = NodeSet(
                ",".join([str(v) for k, v in list(ret_code.items()) if k != 0]))
            raise CommandFailure(
                "Error running '{}' on the following hosts: {}".format(cmd, error_hosts))

    def _teardown_dfuse(self, dfuses):
        """Unmount all the containers that were created for the test"""
        for dfuse in dfuses:
            dfuse.stop()
        self.dfuse = None

    def _setup_challenger(self, test_path, directories):
        """
        Create the paths where the directory trees of the challenger will be
        created.
        """
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
        random_name = "".join(random.choice(letters) for _ in range(8)) #nosec

        return os.path.join(root, "{}{}".format(prefix, random_name))


def _populate_dir_tree():
    """Wrapper function to create a directory tree and its needle files"""
    path = sys.argv[1]
    height = int(sys.argv[2])
    subdirs_per_node = int(sys.argv[3])
    files_per_node = int(sys.argv[4])
    needles = int(sys.argv[5])
    prefix = sys.argv[6]

    print("Populating: {0}".format(path))

    dir_tree = DirTree(path, height, subdirs_per_node, files_per_node)
    dir_tree.set_needles_prefix(prefix)
    dir_tree.set_number_of_needles(needles)
    tree_path = dir_tree.create()
    print("Dir tree created at: {0}".format(tree_path))


if __name__ == '__main__':
    _populate_dir_tree()

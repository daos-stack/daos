#!/usr/bin/python
"""
  (C) Copyright 2021 Intel Corporation.

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
import os
import sys
import string
import random
import general_utils

from ClusterShell.NodeSet import NodeSet
from dfuse_test_base import DfuseTestBase
from command_utils import CommandFailure
from io_utilities import DirTree


class FindCmd(DfuseTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Base FindCmd test class.

    :avocado: recursive
    """

    def test_findcmd(self):
        """Jira ID: DAOS-5563.

        Test Description:
            The purpose of this test is to create some number of POSIX
            containers using dfuse and link them together such that they form
            a tree with a common root directory. Then, run the find(1) command
            to locate specific files in various parts of the tree. If the
            parameter challenger_path is provided, the test will run on it
            and compare the performance of the provided file system against
            DAOS File System.

        :avocado: tags=all,hw,daosio,medium,ib2,full_regression,findcmd
        """
        cont_count = self.params.get("cont_count", '/run/container/*')
        dfs_path = self.params.get("dfs_path", '/run/find_cmd/*')
        samples = self.params.get("samples", '/run/find_cmd/*')
        height = self.params.get("height", '/run/find_cmd/*')
        subdirs_per_node = self.params.get(
            "subdirs_per_node", '/run/find_cmd/*')
        files_per_node = self.params.get("files_per_node", '/run/find_cmd/*')
        needles = self.params.get("needles", '/run/find_cmd/*')
        challenger_path = self.params.get("challenger_path", '/run/find_cmd/*')
        temp_dfs_path = ""

        dfuses = list()
        containers = list()

        self.add_pool(connect=False)

        if not dfs_path:
            temp_dfs_path = self._generate_temp_path_name("/tmp", "dfs_test_")
            dfs_path = temp_dfs_path

        if challenger_path:
            challenger_path = self._generate_temp_path_name(
                challenger_path, "test_")

        def _run_test(test_root, dir_tree_paths):

            self._create_dir_forest(
                dir_tree_paths,
                height,
                subdirs_per_node,
                files_per_node,
                needles)

            test_stats = self._run_test(
                test_root, samples, cont_count, needles)

            return test_stats

        try:
            mount_dirs = self._setup_containers(
                dfs_path, cont_count, dfuses, containers)

            daos_stats = _run_test(dfs_path, mount_dirs)

            if challenger_path:
                challenger_dirs = self._setup_challenger(
                    challenger_path, cont_count)
                challenger_stats = _run_test(challenger_path, challenger_dirs)

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
                self._run_cmd(u"rm -rf {0}".format(challenger_path))

            if temp_dfs_path:
                self._run_cmd(u"rm -rf {0}".format(temp_dfs_path))

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
            self.log.error(
                "Impossible, DAOS is slower running '%s' tag", tag)
            self.fail("Test was expected to pass but it failed")
        else:
            self.log.info(
                "DAOS is equal or faster running '%s' tag", tag)

    def _run_test(self, test_path, samples, containers, needles):
        """
        Perform the actual test, run find(1) to locate the needle files in
        the directory forest. If the number of files located does not match
        the number of expected files. The test fails.
        """
        profiler = general_utils.SimpleProfiler()
        profiler.set_logger(self.log.info)

        def _search_needles(file_name, sample_tag, expected_res):
            self.log.info("Searching pattern: %s", file_name)
            self.log.info("Number of expecting results: %d", expected_res)
            cmd = u"find {0} -name {1} | wc -l | grep {2}".format(test_path,
                                                                  file_name,
                                                                  expected_res)
            profiler.run(self._run_cmd,
                         sample_tag,
                         cmd)

        self.log.info("Sampling path: %s", test_path)

        for i in range(samples):
            self.log.info(
                "Running sample number %d of %d", i + 1, samples)

            prefix = random.randrange(containers - 1)
            suffix = random.randrange(needles - 1)
            file_name = "t{:05d}_*_{:05d}.needle".format(prefix, suffix)
            _search_needles(file_name, "unique_file", 1)

            number = random.randrange(needles - 1)
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
                "Creating container Pool UUID: %s Con UUID: %s",
                self.pool, self.container)
            self.start_dfuse(
                self.hostlist_clients, self.pool, self.container, mount_dir)
            dfuses.append(self.dfuse)
            containers.append(self.container)
            mount_dirs.append(self.dfuse.mount_dir.value)

        return mount_dirs

    def _create_dir_forest(
            self,
            paths,
            height,
            subdirs,
            files_per_node,
            needles):
        """Create a directory tree on each path listed in the paths variable"""

        remote_pythonpath = ":".join(sys.path)

        count = 0
        for path in paths:
            self.log.info("Populating: %s", path)
            prefix = "t{:05d}_".format(count)
            count += 1
            remote_args = "{} {} {} {} {} {}".format(
                path, height, subdirs, files_per_node, needles, prefix)
            dir_tree_cmd = "PYTHONPATH={} python {} {}".format(
                remote_pythonpath, os.path.abspath(__file__), remote_args)
            self._run_cmd(u"{0}".format(dir_tree_cmd))

    def _run_cmd(self, cmd):
        ret_code = general_utils.pcmd(self.hostlist_clients, cmd, timeout=180)
        if 0 not in ret_code:
            error_hosts = NodeSet(
                ",".join([str(v) for k, v in ret_code.items() if k != 0]))
            raise CommandFailure(
                "Error running '{}' on the following hosts: {}".format(
                    cmd, error_hosts))

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
            self._run_cmd(u"mkdir -p {0}".format(dir_name))
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
        random_name = "".join(random.choice(letters) for _ in range(8))

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

    dir_tree = DirTree(
        path,
        height,
        subdirs_per_node,
        files_per_node)
    dir_tree.set_needles_prefix(prefix)
    dir_tree.set_number_of_needles(needles)
    tree_path = dir_tree.create()
    print("Dir tree created at: {0}".format(tree_path))


if __name__ == '__main__':
    _populate_dir_tree()

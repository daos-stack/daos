#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
import shutil
import tempfile
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
            parameter challenger_path is provided. The test will run on the
            path provided and compare the performance of the provided file
            system against DAOS File System.

        :avocado: tags=all,hw,daosio,medium,ib2,full_regression,findcmd
        """
        cont_count = self.params.get("cont_count", '/run/container/*')
        dfs_path = self.params.get("dfs_path", '/run/find_cmd/*')
        samples = self.params.get("samples", '/run/find_cmd/*')
        self.height = self.params.get("height", '/run/find_cmd/*')
        self.subdirs_per_node = self.params.get(
            "subdirs_per_node", '/run/find_cmd/*')
        self.files_per_node = self.params.get(
            "files_per_node", '/run/find_cmd/*')
        self.needles = self.params.get("needles", '/run/find_cmd/*')
        challenger = self.params.get("challenger_path", '/run/find_cmd/*')
        challenger_path = ""

        self.dfuses = list()
        self.containers = list()

        self.add_pool(connect=False)

        try:
            mount_dirs = self._setup_containers(dfs_path, cont_count)
            daos_dir_trees = self._crate_dir_trees(mount_dirs)
            daos_stats = self._run_commands(dfs_path, samples, daos_dir_trees)

            if challenger:
                challenger_path = tempfile.mkdtemp(dir=challenger)
                challenger_dirs = _setup_challenger(
                    cont_count, challenger_path)
                challenger_dir_trees = self._crate_dir_trees(challenger_dirs)
                challenger_stats = self._run_commands(
                    challenger_path, samples, challenger_dir_trees)

        except CommandFailure as error:
            self.log.error("FindCmd Test Failed: %s", str(error))
            self.fail("Test was expected to pass but it failed.\n")
        except Exception as error:
            self.log.error("FindCmd Test Failed: %s", str(error))
            raise
        finally:
            self._teardown_dfuse(self.dfuses)
            self.destroy_containers(self.containers)
            self.pool.destroy()

            if challenger and challenger_path:
                shutil.rmtree(challenger_path, ignore_errors=True)

        self.log.info("DAOS Stats")
        daos_stats.print_stats()

        if challenger:
            self.log.info("Challenger Stats")
            challenger_stats.print_stats()

            self._compare_results(daos_stats, challenger_stats, "all_files")
            self._compare_results(daos_stats, challenger_stats, "same_suffix")
            self._compare_results(daos_stats, challenger_stats, "unique_file")

    def _compare_results(self, daos_stats, challenger_stats, tag):
        daos_max_time, _, _ = daos_stats.get_stat(tag)
        challenger_max_time, _, _ = challenger_stats.get_stat(tag)

        if daos_max_time >= challenger_max_time:
            self.log.warning(
                "Impossible, DAOS is slower running '%s' tag", tag)
        else:
            self.log.info(
                "DAOS is equal or faster running '%s' tag", tag)

    def _run_commands(self, test_path, samples, dir_trees):

        profiler = general_utils.SimpleProfiler()
        profiler.set_logger(self.log.info)

        self.log.info("Sampling path: %s", test_path)

        for i in range(samples):
            self.log.info(
                "Running sample number %d of %d", i + 1, samples)
            profiler.run(
                self._run_cmd,
                "all_files",
                u"find {0} -name *.needle".format(test_path))

            number = random.randrange(self.needles - 1)
            file_name = "*_{:05d}.needle".format(number)
            profiler.run(self._run_cmd, "same_suffix",
                         u"find {0} -name {1}".format(test_path, file_name))

            dir_tree = random.choice(dir_trees)
            file_name, _ = dir_tree.get_probe()
            profiler.run(self._run_cmd, "unique_file",
                         u"find {0} -name {1}".format(test_path, file_name))

        return profiler

    def _setup_containers(self, dfs_path, cont_count):
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
            self.dfuses.append(self.dfuse)
            self.containers.append(self.container)
            mount_dirs.append(mount_dir)

        return mount_dirs

    def _crate_dir_trees(self, paths):
        dir_trees = []

        profiler = general_utils.SimpleProfiler()
        profiler.set_logger(self.log.info)

        for path in paths:
            self.log.info("Populating: %s", path)
            dir_tree = DirTree(
                path,
                self.height,
                self.subdirs_per_node,
                self.files_per_node)
            dir_tree.set_number_of_needles(self.needles)
            tree_path = profiler.run(dir_tree.create, "create_dirtree")
            dir_trees.append(dir_tree)
            self.log.info("Dir tree created at: %s", tree_path)

        profiler.print_stats()
        max_time, min_time, avg_time = profiler.get_stat("create_dirtree")
        self.log.info(
            "Max: %s Min: %s: Avg: %s", str(max_time), str(min_time),
            str(avg_time))

        return dir_trees

    def _run_cmd(self, cmd):
        ret_code = general_utils.pcmd(self.hostlist_clients, cmd, timeout=180)
        if 0 not in ret_code:
            error_hosts = NodeSet(
                ",".join([str(v) for k, v in ret_code.items() if k != 0]))
            raise CommandFailure(
                "Error running '{}' on the following hosts: {}".format(
                    cmd, error_hosts))

    def _teardown_dfuse(self, dfuses):
        for dfuse in dfuses:
            dfuse.stop()
        self.dfuse = None


def _setup_challenger(directories, challenger_path):
    challenger_dirs = []

    for directory in range(directories):
        prefix = "dir_{}_".format(directory)
        tdir = tempfile.mkdtemp(dir=challenger_path, prefix=prefix)
        challenger_dirs.append(tdir)

    return challenger_dirs

#!/usr/bin/python
'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from os.path import join
from data_mover_test_base import DataMoverTestBase


class DmvrPosixSymlinks(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for POSIX DataMover symlink validation

    Test Class Description:
        Tests POSIX DataMover symlink copying and dereferencing.
    :avocado: recursive
    """

    def test_dm_posix_symlinks(self):
        """JIRA id: DAOS-5998
        Test Description:
            Tests copying POSIX symlinks with dcp.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp,dfuse,dfs
        :avocado: tags=dm_posix_symlinks,dm_posix_symlinks_dcp
        """
        self.run_dm_posix_symlinks("DCP")

    def run_dm_posix_symlinks(self, tool):
        """
        Use Cases:
            1. Create pool
            2. Create container
            3. Create symlink structure:
                - Links that point to files
                - Links that point to directories
                - Links that point to other links
                - Links that point forward multiple levels
                - Links that point backward one level
                - Links that are transitive (link -> dir -> link)
            4. Test copying between DAOS and POSIX

        Args:
            tool (str): The DataMover tool to run the test with.
                Must be a valid tool in self.TOOLS.

        NOTE:
            Different symlink structures are created with the
            create_links_* functions, where each structure tests
            some part of the uses cases above.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.dfuse_hosts)

        # Create 1 pool
        pool1 = self.create_pool()

        # Create a special container to hold UNS entries
        uns_cont = self.create_cont(pool1)

        # Test links that point forward
        container1 = self.create_cont(pool1, True, pool1, uns_cont)
        self.run_dm_posix_symlinks_fun(
            pool1, container1, self.create_links_forward, "forward")

        # Test links that point backward
        container2 = self.create_cont(pool1, True, pool1, uns_cont)
        self.run_dm_posix_symlinks_fun(
            pool1, container2, self.create_links_backward, "backward")

        # Test a mix of forward and backward links
        container3 = self.create_cont(pool1, True, pool1, uns_cont)
        self.run_dm_posix_symlinks_fun(
            pool1, container3, self.create_links_mixed, "mixed")

    def run_dm_posix_symlinks_fun(self, pool, cont, link_fun, link_desc):
        """
        Tests copying symlinks with and without --dereference.

        Args:
            pool (TestPool): The pool to use
            cont (TestContainer): The container for both src and dst
            link_fun (str -> void): The function for creating the
                symlink structure. A path is passed for the location.
            link_desc (str): A description about the link_fun.
                Used in logging.
        """
        # Get the dereference param
        do_deref = self.params.get(
            "dereference", "/run/{}/*".format(self.tool.lower()))

        # Use a common test_desc
        test_desc = self.test_id + "({})".format(link_desc)
        test_desc += " (dereference={})".format(str(do_deref))
        self.log.info("Running %s", test_desc)

        # Get a directory for POSIX
        posix_test_path = self.new_posix_test_path()

        # Save some paths and encode the type in the path for easier debugging
        src_daos_dir = "/src_" + link_desc
        src_daos_path = cont.path.value + src_daos_dir
        src_posix_path = join(posix_test_path, "src_" + link_desc)

        # Create the source links
        link_fun(src_daos_path)
        link_fun(src_posix_path)

        if do_deref:
            # Use POSIX cp to create a baseline for dereferencing
            deref_baseline_path = join(posix_test_path, "baseline_" + link_desc)
            self.execute_cmd("cp -r --dereference '{}' '{}'".format(
                src_posix_path, deref_baseline_path))
            diff_src = deref_baseline_path
        else:
            # Just compare against the original
            diff_src = src_posix_path

        # DAOS -> DAOS
        dst_daos_dir = self.new_daos_test_path(create=False)
        self.run_datamover(
            test_desc + " (DAOS->DAOS)",
            "DAOS", src_daos_dir, pool, cont,
            "DAOS", dst_daos_dir, pool, cont)
        self.run_diff(diff_src, cont.path.value + dst_daos_dir, do_deref)

        # DAOS -> POSIX
        dst_posix_path = self.new_posix_test_path(create=False)
        self.run_datamover(
            test_desc + " (DAOS->POSIX)",
            "DAOS", src_daos_dir, pool, cont,
            "POSIX", dst_posix_path)
        self.run_diff(diff_src, dst_posix_path)

        # POSIX -> DAOS
        dst_daos_dir = self.new_daos_test_path(create=False)
        self.run_datamover(
            test_desc + " (POSIX->DAOS)",
            "POSIX", src_posix_path, None, None,
            "DAOS", dst_daos_dir, pool, cont)
        self.run_diff(diff_src, cont.path.value + dst_daos_dir, do_deref)

    def create_links_forward(self, path):
        """
        Creates forward symlinks up to 3 levels deep.

        Args:
            path (str): The path to create the links in

        Description:
            - Links that point to files
            - Links that point to directories
            - Links that point to other links
            - Links that point forward multiple levels deep
            - Links that are transitive (link -> dir -> link)
        """
        cmd_list = [
            "mkdir -p " + path + "/dir1.1/dir1.2/dir1.3",
            "pushd " + path,

            # Level 4: one file
            "echo 'file1.4' > dir1.1/dir1.2/dir1.3/file1.4",

            # Level 3: one file, links to file and dir
            "echo 'file1.3' > dir1.1/dir1.2/file1.3",
            "ln -s file1.3 ./dir1.1/dir1.2/link1.3",
            "ln -s dir1.3 ./dir1.1/dir1.2/link2.3",

            # Level 2: links to level 3
            "ln -s dir1.2/file1.3 ./dir1.1/link1.2",
            "ln -s dir1.2/dir1.3 ./dir1.1/link2.2",
            "ln -s dir1.2/link1.3 ./dir1.1/link3.2",
            "ln -s dir1.2/link2.3 ./dir1.1/link4.2",

            # Level 1: Links to level 2 and level 3
            "ln -s dir1.1/dir1.2 ./link1.1",
            "ln -s dir1.1/link1.2 ./link2.1",
            "ln -s dir1.1/link2.2 ./link3.1",
            "ln -s dir1.1/link3.2 ./link4.1",
            "ln -s dir1.1/link4.2 ./link5.1",
            "ln -s dir1.1/dir1.2/file1.3 ./link6.1",
            "ln -s dir1.1/dir1.2/dir1.3 ./link7.1",
            "ln -s dir1.1/dir1.2/link1.3 ./link8.1",
            "ln -s dir1.1/dir1.2/link2.3 ./link9.1",

            "popd"
        ]
        self.execute_cmd_list(cmd_list)

    def create_links_backward(self, path):
        """
        Creates backward symlinks 1 level deep.
        ../../ is not yet supported.

        Args:
            path (str): The path to create the links in

        Description:
            - Links that point to files
            - Links that point to links
            - Links that point backward, one level up
        """
        cmd_list = [
            "mkdir -p " + path + "/dir1.1/dir1.2/",
            "pushd " + path,

            # Level 1: one file and two links
            "echo 'file1.1' > ./file1.1",
            "ln -s file1.1 ./link1.1",
            "ln -s link1.1 ./link2.1",

            # Level 2: links to level 1
            "ln -s ../file1.1 ./dir1.1/link1.2",
            "ln -s ../link1.1 ./dir1.1/link2.2",

            "popd"
        ]
        self.execute_cmd_list(cmd_list)

    def create_links_mixed(self, path):
        """
        Creates a mix of forward and backward links.
        Level 1 -> Level 3 -> Level 2

        Args:
            path (str): The path to create the links in

        Description:
            - Links that point to files
            - Links that point to links
            - Links that traverse forward and backward
        """
        cmd_list = [
            "mkdir -p " + path + "/dir1.1/dir1.2/",
            "pushd " + path,

            # Level 1: link to level 3
            "ln -s dir1.1/dir1.2/link1.3 ./link1.1",

            # Level 3: one file, link to level 2
            "echo 'file1.3' > ./dir1.1/dir1.2/file1.3",
            "ln -s ../link1.2 ./dir1.1/dir1.2/link1.3",

            # Level 2: link to level 3
            "ln -s dir1.2/file1.3 ./dir1.1/link1.2",

            "popd"
        ]
        self.execute_cmd_list(cmd_list)

    def execute_cmd_list(self, cmd_list):
        """Execute a list of commands, separated by &&.
        Args:
            cmd_list (list): A list of commands to execute.
        """
        cmd = " &&\n".join(cmd_list)
        self.execute_cmd(cmd)

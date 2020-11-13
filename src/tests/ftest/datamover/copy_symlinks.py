#!/usr/bin/python
'''
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
'''
from data_mover_test_base import DataMoverTestBase
from os.path import join

class CopySymlinksTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for basic Datamover symlink validation

    Test Class Description:
        Tests datamover symlink copying and dereferencing.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CopySymlinkssTest object."""
        super(CopySymlinksTest, self).__init__(*args, **kwargs)

    def test_copy_symlinks(self):
        """
        Test Description:
            DAOS-5998: Verify ability to copy and dereference symlinks

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

        NOTE:
            Different symlink structures are created with the
            create_links_* functions, where each structure tests
            some part of the uses cases above.
        :avocado: tags=all,datamover,full_regression
        :avocado: tags=copy_symlinks
        """
        # Start dfuse to hold all pools/containers
        self.dfuse_hosts = self.agent_managers[0].hosts
        self.start_dfuse(self.dfuse_hosts)

        # Create 1 pool
        pool1 = self.create_pool()

        # Create a special container to hold UNS entries
        uns_cont = self.create_cont(pool1)

        # Test links that point forward
        container1 = self.create_cont(pool1, True, pool1, uns_cont)
        self.copy_symlinks_run(
            pool1, container1, self.create_links_forward, "forward")

        # Test links that point backward
        container2 = self.create_cont(pool1, True, pool1, uns_cont)
        self.copy_symlinks_run(
            pool1, container2, self.create_links_backward, "backward")

        # Test a mix of forward and backward links
        container3 = self.create_cont(pool1, True, pool1, uns_cont)
        self.copy_symlinks_run(
            pool1, container3, self.create_links_mixed, "mixed")

    def copy_symlinks_run(self, pool, cont, link_fun, link_desc):
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
        # Use a common test_desc
        test_desc_prefix = "copy_symlinks_run (" + link_desc + ")"
        self.log.info("Running %s", test_desc_prefix)

        # Get a directory for POSIX
        posix_test_path1 = self.new_posix_dir()

        # Save some paths
        src_daos_dir = "/src_" + link_desc
        src_daos_path = cont.path.value + src_daos_dir
        src_posix_path = join(posix_test_path1, "src_" + link_desc)

        # Create the source links
        link_fun(src_daos_path)
        link_fun(src_posix_path)

        # Use POSIX cp to create a baseline for dereferencing
        deref_baseline_path = join(posix_test_path1, "baseline_" + link_desc)
        self.execute_cmd(
            "cp -r --dereference " + src_posix_path + " " + deref_baseline_path)

        # Create a list of each source and destination type to test
        copy_list = [
            ["DAOS", "DAOS"],
            ["DAOS", "POSIX"],
            ["POSIX", "DAOS"]]

        # For each src -> dst combination
        for src_type, dst_type in copy_list:
            src_to_dst = "({} -> {})".format(src_type, dst_type)
            test_desc = test_desc_prefix + " " + src_to_dst

            # Set the source location
            if src_type == "DAOS":
                self.set_src_location(src_type, src_daos_dir, pool, cont)
                diff_left = src_daos_path
            elif src_type == "POSIX":
                self.set_src_location(src_type, src_posix_path)
                diff_left = src_posix_path
            else:
                self.fail("Invalid src type")

            # Run with and without dereferencing
            for do_deref in (False, True):
                # Create a new destination directory
                # and set the destination location
                if dst_type == "DAOS":
                    dst_daos_dir = self.gen_daos_path()
                    dst_path = cont.path.value + dst_daos_dir
                    self.set_dst_location(dst_type, dst_daos_dir, pool, cont)
                elif dst_type == "POSIX":
                    dst_path = self.gen_daos_path(posix_test_path1)
                    self.set_dst_location(dst_type, dst_path)
                else:
                    self.fail("Invalid dst type")

                if do_deref:
                    # Run without dereferencing
                    # Use the src path as the src for the diff
                    self.dm_cmd.dereference.update(False)
                    self.run_datamover(test_desc + " (no dereference)")
                    self.run_diff(diff_left, dst_path, False)
                else:
                    # Run with dereferencing
                    # Use the baseline as the src for the diff
                    self.dm_cmd.dereference.update(True)
                    self.run_datamover(test_desc + " (dereference)")
                    self.run_diff(deref_baseline_path, dst_path, True)

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
        """
        Executes a list of commands.

        Args:
            cmd_list (list): A list of strings of commands
        """
        cmd = "; \\\n".join(cmd_list)
        self.execute_cmd(cmd)

    # TODO move this to TestBase and formalize
    def run_diff(self, src, dst, dereference):
        """
        Runs diff on two directories.

        Args:
            dereference (bool): Whether or not diff should dereference
                symlinks.
        """
        if dereference:
            self.execute_cmd("diff -r " + src + " " + dst)
        else:
            self.execute_cmd("diff -r --no-dereference " + src + " " + dst)

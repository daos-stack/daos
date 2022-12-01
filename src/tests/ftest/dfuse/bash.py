#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from ClusterShell.NodeSet import NodeSet

import general_utils
from dfuse_test_base import DfuseTestBase
from exception_utils import CommandFailure


class Cmd(DfuseTestBase):
    # pylint: disable=too-few-public-methods,too-many-ancestors
    """Base Cmd test class.

    :avocado: recursive
    """

    def test_bashcmd(self):
        """Jira ID: DAOS-3508.

        Test Description:
            Purpose of this test is to mount different mount points of dfuse
            for different container and pool sizes and perform basic bash
            commands.

        Use cases:
            Following list of bash commands have been incorporated
            as part of this test: mkdir, touch, ls, chmod, rm, dd, stat,
            cp, cmp, mv, rmdir.
              Create a directory.
              Create a file under that directory.
              List the created file.
              Remove the file.
              Write a file to the dfuse mounted location using dd.
              List the written file to verify if it's create.
              Verify the file created is of right size as desired.
              Copy the file
              Compare the copied file with original to verify the
              content is same.
              Remove copied file.
              Rename file
              Verify renamed file exist using list.
              Verify dfuse support for '.'
              Verify dfuse support for '..'
              Remove renamed file
              Remove a directory

        :avocado: tags=all,pr,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=dfuse
        :avocado: tags=Cmd,test_bashcmd
        """
        dir_name = self.params.get("dirname", '/run/bashcmd/*')
        file_name1 = self.params.get("filename1", '/run/bashcmd/*')
        file_name2 = self.params.get("filename2", '/run/bashcmd/*')
        dd_count = self.params.get("dd_count", '/run/bashcmd/*')
        dd_blocksize = self.params.get("dd_blocksize", '/run/bashcmd/*')
        pool_count = self.params.get("pool_count", '/run/pool/*')
        cont_count = self.params.get("cont_count", '/run/container/*')

        # Create a pool if one does not already exist.
        for _ in range(pool_count):
            self.add_pool(connect=False)
            # perform test for multiple containers.
            for count in range(cont_count):
                self.add_container(self.pool)
                mount_dir = "/tmp/{}_daos_dfuse{}".format(self.pool.uuid, count)
                self.start_dfuse(
                    self.hostlist_clients, self.pool, self.container, mount_dir=mount_dir)
                abs_dir_path = os.path.join(
                    self.dfuse.mount_dir.value, dir_name)
                abs_file_path1 = os.path.join(abs_dir_path, file_name1)
                abs_file_path2 = os.path.join(abs_dir_path, file_name2)
                # list of commands to be executed.
                commands = ["mkdir -p {}".format(abs_dir_path),
                            "touch {}".format(abs_file_path1),
                            "ls -a {}".format(abs_file_path1),
                            "rm {}".format(abs_file_path1),
                            "dd if=/dev/zero of={} count={} bs={}".format(
                                abs_file_path1, dd_count, dd_blocksize),
                            "ls -al {}".format(abs_file_path1),
                            "filesize=$(stat -c%s '{}');\
                            if (( filesize != {}*{} )); then exit 1;\
                            fi".format(abs_file_path1, dd_count, dd_blocksize),
                            "cp -r {} {}".format(abs_file_path1, abs_file_path2),
                            "cmp --silent {} {}".format(abs_file_path1, abs_file_path2),
                            "rm {}".format(abs_file_path2),
                            "mv {} {}".format(abs_file_path1, abs_file_path2),
                            "ls -al {}".format(abs_file_path2),
                            "ls -al {}/.".format(abs_dir_path),
                            "ls -al {}/..".format(abs_dir_path),
                            "rm {}".format(abs_file_path2),
                            "rmdir {}".format(abs_dir_path)]
                for cmd in commands:
                    try:
                        # execute bash cmds
                        ret_code = general_utils.pcmd(
                            self.hostlist_clients, cmd, timeout=30)
                        if 0 not in ret_code:
                            error_hosts = NodeSet(
                                ",".join(
                                    [str(node_set) for code, node_set in
                                     list(ret_code.items()) if code != 0]))
                            raise CommandFailure(
                                "Error running '{}' on the following hosts: {}".format(
                                    cmd, error_hosts))
                    # report error if any command fails
                    except CommandFailure as error:
                        self.log.error("BashCmd Test Failed: %s",
                                       str(error))
                        self.fail("Test was expected to pass but "
                                  "it failed.\n")

                # stop dfuse
                self.stop_dfuse()
                # destroy container
                self.container.destroy()
            # destroy pool
            self.pool.destroy()

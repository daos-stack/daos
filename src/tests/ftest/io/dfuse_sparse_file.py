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
import general_utils

from ClusterShell.NodeSet import NodeSet
from command_utils import CommandFailure
from ior_test_base import IorTestBase


class DfuseSparseFile(IorTestBase):
    """Dfuse Sparse File base class

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DfuseSparseFile object."""
        super(DfuseSparseFile, self).__init__(*args, **kwargs)
        self.space_before = None
        self.ret = None

    def tearDown(self):
        """Tear down each test case."""
        try:
            if self.dfuse:
                self.dfuse.stop()
        finally:
            # Stop the servers and agents
            super(DfuseSparseFile, self).tearDown()

    def get_nvme_free_space(self, display=True):
        """Display pool free space.

          Args:
            display (bool): boolean to display output of free space.

          Returns:
            free_space_nvme (int): Free space available in nvme.
        """
        free_space_nvme = self.pool.get_pool_free_space("nvme")
        if display:
            self.log.info("Free nvme space: %s", free_space_nvme)

        return free_space_nvme

    def execute_cmd(self, cmd, fail_on_err=True, display_output=True):
        """Execute cmd using general_utils.pcmd

          Args:
            cmd (str): String command to be executed
            fail_on_err (bool): Boolean for whether to fail the test if command
                                execution returns non zero return code.
            display_output (bool): Boolean for whether to display output.
        """
        try:
            # execute bash cmds
            self.ret = general_utils.pcmd(
                self.hostlist_clients, cmd, verbose=display_output, timeout=300)
            if 0 not in self.ret:
                error_hosts = NodeSet(
                    ",".join(
                        [str(node_set) for code, node_set in
                         self.ret.items() if code != 0]))
                if fail_on_err:
                    raise CommandFailure(
                        "Error running '{}' on the following "
                        "hosts: {}".format(cmd, error_hosts))

        # report error if any command fails
        except CommandFailure as error:
            self.log.error("DfuseSparseFile Test Failed: %s",
                           str(error))
            self.fail("Test was expected to pass but "
                      "it failed.\n")

    def test_dfusesparsefile(self):
        """Jira ID: DAOS-3768

        Test Description:
            Purpose of this test is to mount dfuse and verify behaviour
            when reading sparse file.
        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Create sparsefile.txt and set it's max size to max available
            space in nvme.
            Write to it's first byte.
            Then, write to it's 1024th Byte
            Verify both the bytes got written with desired data.
            Verify, the bytes between 1std byte and 1024th byte are empty.
            Now try to read the file from it's last 512 bytes till EOF.
            This should return EOF, otherwise fail the test.
        :avocado: tags=all,hw,daosio,small,full_regression,dfusesparsefile
        """
        # Create a pool, container and start dfuse.
        self.create_pool()
        self.create_cont()
        self._start_dfuse()

        # get scm space before write
        self.space_before = self.get_nvme_free_space()

        # create large fle and perform write to it so that if goes out of
        # space.
        sparse_file = unicode(self.dfuse.mount_dir.value + "/" +
                              "sparsefile.txt")
        self.execute_cmd(u"touch {}".format(sparse_file))
        self.log.info("File size (in bytes) before truncate: %s",
                      os.path.getsize(sparse_file))

        # open file
        file_obj = open(sparse_file, 'r+')
        # set file size to max available nvme size
        file_obj.truncate(self.space_before)
        self.log.info("File size (in bytes) after truncate: %s",
                      os.path.getsize(sparse_file))
        # verifying the file size got set to desired value
        self.assertTrue(os.path.getsize(sparse_file) == self.space_before)

        # write to the first byte of the file with char 'A'
        dd_first_byte = u"echo 'A' | dd conv=notrunc of={} bs=1 count=1".\
                        format(sparse_file)
        self.execute_cmd(dd_first_byte)
        self.log.info("File size (in bytes) after writing first byte: %s",
                      os.path.getsize(sparse_file))
        # verify file did not got overriten after dd write.
        self.assertTrue(os.path.getsize(sparse_file) == self.space_before)


        # write to the 1024th byte position of the file
        dd_1024_byte = u"echo 'A' | dd conv=notrunc of={} obs=1 seek=1023 \
                       bs=1 count=1".format(sparse_file)
        self.execute_cmd(dd_1024_byte)
        self.log.info("File size (in bytes) after writing first byte: %s",
                      os.path.getsize(sparse_file))
        # verify file did not got overriten after dd write.
        self.assertTrue(os.path.getsize(sparse_file) == self.space_before)


        # Obtainer the value of 1st byte and 1024th bytes in the file and
        # compare their values, they should be same.
        check_first_byte = file_obj.read(1)
        file_obj.seek(1022, 1)
        check_1024th_byte = file_obj.read(1)
        self.assertTrue(check_first_byte == check_1024th_byte)

        # check the middle 1022 bytes if they are filled with zeros
        middle_1022_bytes = u"cmp --ignore-initial=1 --bytes=1022 {} {}".\
                                  format(sparse_file, "/dev/zero")
        self.execute_cmd(middle_1022_bytes)

        # read last 512 bytes which should be zeros till end of file.
        ignore_bytes = self.space_before - 512
        read_till_eof = u"cmp --ignore-initial={} {} {}".format(
            ignore_bytes, sparse_file, "/dev/zero")
        self.execute_cmd(read_till_eof, False)
        # fail the test if the above command is successful.
        if 0 in self.ret:
            self.fail("read_till_eof command was supposed to fail. "
                      "But it completed successfully.")

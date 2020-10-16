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

import time
from ior_test_base import IorTestBase


class DfuseSpaceCheck(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Base Parallel IO test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DfuseSpaceCheck object."""
        super(DfuseSpaceCheck, self).__init__(*args, **kwargs)
        self.space_before = None
        self.block_size = None

    def get_nvme_free_space(self, display=True):
        """Display pool free space.

        Args:
            display (bool): boolean to display output of free space.

        Returns:
            int: Free space available in nvme.

        """
        free_space_nvme = self.pool.get_pool_free_space("nvme")
        if display:
            self.log.info("Free nvme space: %s", free_space_nvme)

        return free_space_nvme

    def check_aggregation(self):
        """Check Aggregation for 240 secs max, else fail the test."""
        counter = 1
        while self.get_nvme_free_space() != self.space_before:
            # try to wait for 4 x 60 secs for aggregation to be completed or
            # else exit the test with a failure.
            if counter > 4:
                self.log.info("Free space when test terminated: %s",
                              self.get_nvme_free_space())
                self.fail("Aggregation did not complete as expected")
            time.sleep(60)
            counter += 1

    def write_multiple_files(self):
        """Write multiple files.

        Returns:
            int: Total number of files created before going out of space.

        """
        file_count = 0
        while self.get_nvme_free_space(False) >= self.block_size:
            file_loc = str(self.dfuse.mount_dir.value +
                           "/largefile_{}.txt".format(file_count))
            write_dd_cmd = u"dd if=/dev/zero of={} bs={} count=1".format(
                file_loc, self.block_size)
            if 0 in self.execute_cmd(write_dd_cmd, False, False):
                file_count += 1

        return file_count

    def test_dfusespacecheck(self):
        """Jira ID: DAOS-3777.

        Test Description:
            Purpose of this test is to mount dfuse and verify aggregation
            to return space when pool is filled with once large file and
            once with small files.
        Use cases:
            Create Pool
            Create Posix container
            Mount dfuse
            Create largefile.txt and write to it until pool is out of space.
            Remove largefile.txt and wait for aggregation to return all the
            space back.
            Now create many small files until pool is out of space again and
            store the number of files created.
            Remove all the small files created and wait for aggregation to
            return all the space back.
            Now create small files again until pool is out of space and check,
            whether out of space happens at the same file count as before.
        :avocado: tags=all,hw,daosio,small,full_regression,dfusespacecheck
        """
        # get test params for cont and pool count
        self.block_size = self.params.get("block_size",
                                          '/run/dfusespacecheck/*')

        # Create a pool, container and start dfuse.
        self.create_pool()
        self.create_cont()
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        # get scm space before write
        self.space_before = self.get_nvme_free_space()

        # create large file and perform write to it so that if goes out of
        # space.
        large_file = str(self.dfuse.mount_dir.value + "/" + "largefile.txt")
        cmd = u"touch {}".format(large_file)
        self.execute_cmd(cmd)
        dd_count = ((self.space_before / self.block_size) + 1)
        write_dd_cmd = u"dd if=/dev/zero of={} bs={} count={}".format(
            large_file, self.block_size, dd_count)
        self.execute_cmd(write_dd_cmd, False)

        # store free space after write and remove the file
        rm_large_file = u"rm -rf {}".format(large_file)
        self.execute_cmd(rm_large_file)

        # Check if aggregation is complete.
        self.check_aggregation()

        # Once aggregation is complete, write multiple files of small size
        # until pool is out of space and store how many files were created.
        file_count1 = self.write_multiple_files()

        # remove all the small files created above.
        self.execute_cmd(u"rm -rf {}/*".format(self.dfuse.mount_dir.value))

        # Check for aggregation to complete after file removal.
        self.check_aggregation()

        # Write small sized multiple files again until pool is out of space
        # and store the number of files created.
        file_count2 = self.write_multiple_files()

        # Check if both the files counts is equal. If not, fail the test.
        if file_count1 != file_count2:
            self.fail("Space was not returned completely after re-writing the "
                      "same number of files after deletion")

#!/usr/bin/python
"""
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""

from getpass import getuser
import paramiko

from general_utils import get_remote_file_size
from ior_test_base import IorTestBase


class DfuseSparseFile(IorTestBase):
    # pylint: disable=too-many-ancestors,too-few-public-methods
    """Dfuse Sparse File base class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a DfuseSparseFile object."""
        super(DfuseSparseFile, self).__init__(*args, **kwargs)
        self.space_before = None

    def test_dfusesparsefile(self):
        """Jira ID: DAOS-3768.

        Test Description:
            Purpose of this test is to mount dfuse and verify behavior
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
            Verify, the bytes between 1st byte and 1024th byte are empty.
            Now try to read the file from it's last 512 bytes till EOF.
            This should return EOF, otherwise fail the test.
        :avocado: tags=all,hw,daosio,small,full_regression,dfusesparsefile
        """
        # Create a pool, container and start dfuse.
        self.create_pool()
        self.create_cont()
        self.start_dfuse(self.hostlist_clients, self.pool, self.container)

        # get scm space before write
        self.space_before = self.pool.get_pool_free_space("nvme")

        # create large file and perform write to it so that if goes out of
        # space.
        sparse_file = str(self.dfuse.mount_dir.value + "/" +
                          "sparsefile.txt")
        self.execute_cmd(u"touch {}".format(sparse_file))
        self.log.info("File size (in bytes) before truncate: %s",
                      get_remote_file_size(
                          self.hostlist_clients[0], sparse_file))

        # create and open a connection on remote node to open file on that
        # remote node
        ssh = paramiko.SSHClient()
        ssh.load_system_host_keys()
        ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())
        ssh.connect(self.hostlist_clients[0], username=getuser())
        sftp = ssh.open_sftp()

        # open remote file
        file_obj = sftp.open(sparse_file, 'r+')
        # set file size to max available nvme size
        sftp.truncate(sparse_file, self.space_before)
        fsize_after_truncate = get_remote_file_size(self.hostlist_clients[0],
                                                    sparse_file)
        self.log.info("File size (in bytes) after truncate: %s",
                      fsize_after_truncate)
        # verifying the file size got set to desired value
        self.assertTrue(fsize_after_truncate == self.space_before)

        # write to the first byte of the file with char 'A'
        dd_first_byte = u"echo 'A' | dd conv=notrunc of={} bs=1 count=1".\
                        format(sparse_file)
        self.execute_cmd(dd_first_byte)
        fsize_write_1stbyte = get_remote_file_size(self.hostlist_clients[0],
                                                   sparse_file)
        self.log.info("File size (in bytes) after writing first byte: %s",
                      fsize_write_1stbyte)
        # verify file did not got overriten after dd write.
        self.assertTrue(fsize_write_1stbyte == self.space_before)

        # write to the 1024th byte position of the file
        dd_1024_byte = u"echo 'A' | dd conv=notrunc of={} obs=1 seek=1023 \
                       bs=1 count=1".format(sparse_file)
        self.execute_cmd(dd_1024_byte)
        fsize_write_1024thwrite = get_remote_file_size(self.hostlist_clients[0],
                                                       sparse_file)
        self.log.info("File size (in bytes) after writing 1024th byte: %s",
                      fsize_write_1024thwrite)
        # verify file did not got overriten after dd write.
        self.assertTrue(fsize_write_1024thwrite == self.space_before)

        # Obtain the value of 1st byte and 1024th byte in the file and
        # compare their values, they should be same.
        check_first_byte = file_obj.read(1)
        file_obj.seek(1022, 1)
        check_1024th_byte = file_obj.read(1)
        self.assertTrue(check_first_byte == check_1024th_byte)

        # check the middle 1022 bytes if they are filled with zeros
        middle_1022_bytes = \
            u"cmp --ignore-initial=1 --bytes=1022 {} {}".format(
                sparse_file, "/dev/zero")
        self.execute_cmd(middle_1022_bytes)

        # read last 512 bytes which should be zeros till end of file.
        ignore_bytes = self.space_before - 512
        read_till_eof = u"cmp --ignore-initial={} {} {}".format(
            ignore_bytes, sparse_file, "/dev/zero")
#        self.execute_cmd(read_till_eof, False)
        # fail the test if the above command is successful.
        if 0 in self.execute_cmd(read_till_eof, False):
            self.fail("read_till_eof command was supposed to fail. "
                      "But it completed successfully.")

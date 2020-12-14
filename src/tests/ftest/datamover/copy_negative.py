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
from os.path import join, sep


class CopyNegativeTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for Datamover negative testing.

    Test Class Description:
        Tests the following cases:
            Bad parameters.
            Simple error checking.
    :avocado: recursive
    """

    # DCP error codes
    MFU_ERR = "MFU_ERR(-1000)"
    MFU_ERR_INVAL_ARG = "MFU_ERR(-1001)"
    MFU_ERR_DCP = "MFU_ERR(-1100)"
    MFU_ERR_DCP_COPY = "MFU_ERR(-1101)"
    MFU_ERR_DAOS = "MFU_ERR(-4000)"
    MFU_ERR_DAOS_INVAL_ARG = "MFU_ERR(-4001)"

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyNegativeTest, self).setUp()

        # Get the parameters
        self.flags_write = self.params.get(
            "flags_write", "/run/ior/copy_negative/*")
        self.block_size = self.params.get(
            "block_size", "/run/ior/*")
        self.block_size_large = self.params.get(
            "block_size_large", "/run/ior/copy_negative/*")
        self.test_file = self.params.get(
            "test_file", "/run/ior/copy_negative/*")

        # Setup the directory structures
        self.posix_test_paths.append(join(self.workdir, "posix_test") + sep)
        self.posix_test_file = join(self.posix_test_paths[0], self.test_file)
        self.daos_test_path = "/"
        self.daos_test_file = join(self.daos_test_path, self.test_file)

        # Create the directories
        cmd = "mkdir -p {}".format(self.get_posix_test_path_string())
        self.execute_cmd(cmd)

    def test_copy_bad_params(self):
        """Jira ID: DAOS-5515
        Test Description:
            (1) Bad parameter: required argument
            (2) Bad parameter: source is destination.
            (3) Bad parameter: daos-prefix is invalid.
            (4) Bad parameter: UUID, UNS, or POSIX path is invalid.
        :avocado: tags=all,datamover,full_regression
        :avocado: tags=copy_negative,copy_bad_params
        """
        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.dfuse_hosts)

        # Create a test pool
        pool1 = self.create_pool()

        # Create a special container to hold UNS entries
        uns_cont = self.create_cont(pool1)

        # Create a test container
        container1 = self.create_cont(pool1, True, pool1, uns_cont)

        # Create test files
        self.set_ior_location_and_run("POSIX", self.posix_test_file,
                                      flags=self.flags_write)
        self.set_ior_location_and_run("DAOS_UUID", self.daos_test_file,
                                      pool1, container1,
                                      flags=self.flags_write)

        # Bad parameter: required arguments.
        # These tests use the same valid destination parameters,
        # but varying invalid source parameters.
        self.set_dst_location("POSIX", self.posix_test_paths[0])

        self.set_src_location("DAOS_UUID", "/", None, container1)
        self.run_datamover(
            test_desc="copy_bad_params (source cont but no source pool)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("DAOS_UUID", "/", pool1, None)
        self.run_datamover(
            test_desc="copy_bad_params (source pool but no source cont)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("DAOS_UUID", "/", pool1.uuid, container1)
        self.run_datamover(
            test_desc="copy_bad_params (source pool but no source svcl)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # Bad parameter: required arguments.
        # These tests use the same valid source parameters,
        # but varying invalid destination parameters.
        self.set_src_location("POSIX", self.posix_test_paths[0])

        self.set_dst_location("DAOS_UUID", "/", None, container1)
        self.run_datamover(
            test_desc="copy_bad_params (dest cont but no dest pool)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_dst_location("DAOS_UUID", "/", pool1.uuid, container1)
        self.run_datamover(
            test_desc="copy_bad_params (dest pool but no dest svcl)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # Bad parameter: required arguments.
        # These tests use missing prefix/UNS parameters.
        self.set_dst_location("POSIX", self.posix_test_file)
        self.set_src_location("DAOS_UNS", self.daos_test_file,
                              None, container1)
        self.run_datamover(
            test_desc="copy_bad_params (source prefix but no svcl)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_dst_location("POSIX", self.posix_test_paths[0])
        self.set_src_location("DAOS_UNS", "/", None, container1)
        self.run_datamover(
            test_desc="copy_bad_params (source UNS but no svcl)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("POSIX", self.posix_test_file)
        self.set_dst_location("DAOS_UNS", self.daos_test_file,
                              None, container1)
        self.run_datamover(
            test_desc="copy_bad_params (dest prefix but no svcl)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("POSIX", self.posix_test_paths[0])
        self.set_dst_location("DAOS_UNS", "/", None, container1)
        self.run_datamover(
            test_desc="copy_bad_params (dest UNS but no svcl)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # (2) Bad parameter: source is destination.
        # These tests use the same source and destination.
        self.set_src_location("DAOS_UUID", "/", pool1, container1)
        self.set_dst_location("DAOS_UUID", "/", pool1, container1)
        self.run_datamover(
            test_desc="copy_bad_params (UUID source is UUID dest)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("DAOS_UNS", "/", pool1, container1)
        self.set_dst_location("DAOS_UNS", "/", pool1, container1)
        self.run_datamover(
            test_desc="copy_bad_params (UNS source is UNS dest)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("DAOS_UUID", "/", pool1, container1)
        self.set_dst_location("DAOS_UNS", "/", pool1, container1)
        self.run_datamover(
            test_desc="copy_bad_params (UUID source is UNS dest)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("DAOS_UNS", "/", pool1, container1)
        self.set_dst_location("DAOS_UUID", "/", pool1, container1)
        self.run_datamover(
            test_desc="copy_bad_params (UNS source is UUID dest)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3) Bad parameter: daos-prefix is invalid.
        # These tests use invalid prefixes.
        self.set_dst_location("POSIX", self.posix_test_paths[0])
        self.set_src_location("DAOS_UNS", "/", pool1, container1)
        self.dm_cmd.daos_prefix.update("/fake/prefix")
        self.dm_cmd.src_path.update("/fake/prefix/dir")
        self.run_datamover(
            test_desc="copy_bad_params (source prefix is not UNS path)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("POSIX", self.posix_test_paths[0])
        self.set_dst_location("DAOS_UNS", "/", pool1, container1)
        self.dm_cmd.daos_prefix.update("/fake/prefix")
        self.dm_cmd.dest_path.update("/fake/prefix/dir")
        self.run_datamover(
            test_desc="copy_bad_params (dest prefix is not UNS path)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_dst_location("POSIX", self.posix_test_paths[0])
        self.set_src_location("DAOS_UNS", "/temp", pool1, container1)
        self.dm_cmd.src_path.update("/fake/fake/fake")
        self.run_datamover(
            test_desc="copy_bad_params (UNS prefix doesn't match src or dst)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_dst_location("POSIX", self.posix_test_paths[0])
        self.set_src_location("DAOS_UNS", "/temp", pool1, container1)
        src_path = "/fake/fake" + str(self.dm_cmd.daos_prefix.value)
        self.dm_cmd.src_path.update(src_path)
        self.run_datamover(
            test_desc="copy_bad_params (src prefix is substring, not prefix)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("POSIX", self.posix_test_paths[0])
        self.set_dst_location("DAOS_UNS", "/temp", pool1, container1)
        dst_path = "/fake/fake" + str(self.dm_cmd.daos_prefix.value)
        self.dm_cmd.dest_path.update(dst_path)
        self.run_datamover(
            test_desc="copy_bad_params (dst prefix is substring, not prefix)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_dst_location("DAOS_UUID", "/", pool1, container1)
        self.set_src_location("POSIX", self.posix_test_paths[0])
        self.dm_cmd.daos_prefix.update(self.posix_test_paths[0])
        self.run_datamover(
            test_desc="copy_bad_params (prefix is on POSIX src)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_src_location("DAOS_UUID", "/", pool1, container1)
        self.set_dst_location("POSIX", self.posix_test_paths[0])
        self.dm_cmd.daos_prefix.update(self.posix_test_paths[0])
        self.run_datamover(
            test_desc="copy_bad_params (prefix is on POSIX dst)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # (4) Bad parameter: UUID, UNS, or POSIX path does not exist.
        # These tests use parameters that do not exist. """
        self.set_dst_location("POSIX", self.posix_test_paths[0])
        self.set_src_location("DAOS_UUID", "/", pool1, container1)
        self.dm_cmd.daos_src_pool.update(str(self.gen_uuid()))
        self.run_datamover(
            test_desc="copy_bad_params (src pool uuid does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS)

        self.set_dst_location("POSIX", self.posix_test_paths[0])
        self.set_src_location("DAOS_UUID", "/", pool1, container1)
        self.dm_cmd.daos_src_cont.update(str(self.gen_uuid()))
        self.run_datamover(
            test_desc="copy_bad_params (src cont uuid does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS)

        self.set_src_location("POSIX", self.posix_test_paths[0])
        self.set_dst_location("DAOS_UUID", "/", pool1, container1)
        self.dm_cmd.daos_dst_pool.update(str(self.gen_uuid()))
        self.run_datamover(
            test_desc="copy_bad_params (dst pool uuid does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS)

        self.set_dst_location("POSIX", self.posix_test_paths[0])
        self.set_src_location("DAOS_UUID", "/fake/fake", pool1, container1)
        self.run_datamover(
            test_desc="copy_bad_params (src cont path does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_INVAL_ARG)

        self.set_src_location("POSIX", self.posix_test_paths[0])
        self.set_dst_location("DAOS_UUID", "/fake/fake", pool1, container1)
        self.run_datamover(
            test_desc="copy_bad_params (dst cont path does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_INVAL_ARG)

        self.set_src_location("POSIX", "/fake/fake/fake")
        self.set_dst_location("DAOS_UUID", "/", pool1, container1)
        self.run_datamover(
            test_desc="copy_bad_params (src posix path does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_INVAL_ARG)

        self.set_dst_location("POSIX", "/fake/fake/fake")
        self.set_src_location("DAOS_UUID", "/", pool1, container1)
        self.run_datamover(
            test_desc="copy_bad_params (dst posix path does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_INVAL_ARG)

    def test_copy_error_check(self):
        """Jira ID: DAOS-5515
        Test Description:
            (1) Error checking: destination filename is invalid.
            (2) Error checking: destination pool out of space.
        :avocado: tags=all,datamover,full_regression
        :avocado: tags=copy_negative,copy_error_check
        """
        # Create pool and containers
        pool1 = self.create_pool()
        container1 = self.create_cont(pool1)

        # Create source file
        self.set_ior_location_and_run("DAOS_UUID", self.daos_test_file,
                                      pool1, container1,
                                      flags=self.flags_write)

        self.set_src_location("DAOS_UUID", "/", pool1, container1)
        # Use a really long filename
        dst_path = join(self.posix_test_paths[0], "d"*300)
        self.set_dst_location("POSIX", dst_path)
        self.run_datamover(
            test_desc="copy_error_check (filename is too long)",
            expected_rc=1,
            expected_output=[self.MFU_ERR_DCP_COPY, "errno=36"])

        # Write a large file to POSIX
        self.ior_cmd.block_size.update(self.block_size_large)
        self.set_ior_location_and_run("POSIX", self.posix_test_file,
                                      flags=self.flags_write)
        self.set_src_location("POSIX", self.posix_test_paths[0])
        self.set_dst_location("DAOS_UUID", "/", pool1, container1)
        self.run_datamover(
            test_desc="copy_error_check (dst pool out of space)",
            expected_rc=1,
            expected_output=[self.MFU_ERR_DCP_COPY, "errno=28"])

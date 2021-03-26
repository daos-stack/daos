#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from os.path import join
from apricot import skipForTicket


class CopyNegativeTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for POSIX DataMover negative testing.

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
        super().setUp()

        # Get the parameters
        self.test_file = self.ior_cmd.test_file.value

        # Setup the directory structures
        self.new_posix_test_path()
        self.posix_test_file = join(self.posix_test_paths[0], self.test_file)
        self.daos_test_path = "/"
        self.daos_test_file = join(self.daos_test_path, self.test_file)

    @skipForTicket("DAOS-6355")
    def test_copy_bad_params_dcp(self):
        """Jira ID: DAOS-5515
        Test Description:
            Test POSIX copy with invalid parameters.
            This uses the dcp tool.
            (1) Bad parameter: required argument
            (2) Bad parameter: source is destination.
            (3) Bad parameter: daos-prefix is invalid.
            (4) Bad parameter: UUID, UNS, or POSIX path is invalid.
        :avocado: tags=all,full_regression
        :avocado: tags=datamover
        :avocado: tags=copy_negative,copy_bad_params_dcp
        """
        self.set_tool("DCP")

        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.dfuse_hosts)

        # Create a test pool
        pool1 = self.create_pool()

        # Create a special container to hold UNS entries
        uns_cont = self.create_cont(pool1)

        # Create a test container
        cont1 = self.create_cont(pool1, True, pool1, uns_cont)

        # Create test files
        self.run_ior_with_params("POSIX", self.posix_test_file)
        self.run_ior_with_params("DAOS_UUID", self.daos_test_file,
                                 pool1, cont1)

        # Bad parameter: required arguments.
        # These tests use missing arguments
        self.run_datamover(
            "copy_bad_params (source cont but no source pool)",
            "DAOS_UUID", "/", None, cont1,
            "POSIX", self.posix_test_paths[0],
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            "copy_bad_params (source pool but no source cont)",
            "DAOS_UUID", "/", pool1, None,
            "POSIX", self.posix_test_paths[0],
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # Bad parameter: required arguments.
        # This test uses invalid destination parameters.
        self.run_datamover(
            "copy_bad_params (dest cont but no dest pool)",
            "POSIX", self.posix_test_paths[0], None, None,
            "DAOS_UUID", "/", None, cont1,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # (2) Bad parameter: source is destination.
        # These tests use the same source and destination.
        self.run_datamover(
            "copy_bad_params (UUID source is UUID dest)",
            "DAOS_UUID", "/", pool1, cont1,
            "DAOS_UUID", "/", pool1, cont1,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            "copy_bad_params (UNS source is UNS dest)",
            "DAOS_UNS", "/", pool1, cont1,
            "DAOS_UNS", "/", pool1, cont1,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            "copy_bad_params (UUID source is UNS dest)",
            "DAOS_UUID", "/", pool1, cont1,
            "DAOS_UNS", "/", pool1, cont1,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            "copy_bad_params (UNS source is UUID dest)",
            "DAOS_UNS", "/", pool1, cont1,
            "DAOS_UUID", "/", pool1, cont1,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3) Bad parameter: daos-prefix is invalid.
        # These tests use invalid prefixes.
        self.set_datamover_params(
            "DAOS_UNS", "/", pool1, cont1,
            "POSIX", self.posix_test_paths[0])
        self.dcp_cmd.daos_prefix.update("/fake/prefix")
        self.dcp_cmd.src_path.update("/fake/prefix/dir")
        self.run_datamover(
            test_desc="copy_bad_params (source prefix is not UNS path)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_datamover_params(
            "POSIX", self.posix_test_paths[0], None, None,
            "DAOS_UNS", "/", pool1, cont1)
        self.dcp_cmd.daos_prefix.update("/fake/prefix")
        self.dcp_cmd.dst_path.update("/fake/prefix/dir")
        self.run_datamover(
            test_desc="copy_bad_params (dest prefix is not UNS path)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_datamover_params(
            "DAOS_UNS", "/temp", pool1, cont1,
            "POSIX", self.posix_test_paths[0])
        self.dcp_cmd.src_path.update("/fake/fake/fake")
        self.run_datamover(
            test_desc="copy_bad_params (UNS prefix doesn't match src or dst)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_datamover_params(
            "DAOS_UNS", "/temp", pool1, cont1,
            "POSIX", self.posix_test_paths[0])
        src_path = "/fake/fake" + str(self.dcp_cmd.daos_prefix.value)
        self.dcp_cmd.src_path.update(src_path)
        self.run_datamover(
            test_desc="copy_bad_params (src prefix is substring, not prefix)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_datamover_params(
            "POSIX", self.posix_test_paths[0], None, None,
            "DAOS_UNS", "/temp", pool1, cont1)
        dst_path = "/fake/fake" + str(self.dcp_cmd.daos_prefix.value)
        self.dcp_cmd.dst_path.update(dst_path)
        self.run_datamover(
            test_desc="copy_bad_params (dst prefix is substring, not prefix)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_datamover_params(
            "POSIX", self.posix_test_paths[0], None, None,
            "DAOS_UUID", "/", pool1, cont1)
        self.dcp_cmd.daos_prefix.update(self.posix_test_paths[0])
        self.run_datamover(
            test_desc="copy_bad_params (prefix is on POSIX src)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.set_datamover_params(
            "DAOS_UUID", "/", pool1, cont1,
            "POSIX", self.posix_test_paths[0])
        self.dcp_cmd.daos_prefix.update(self.posix_test_paths[0])
        self.run_datamover(
            test_desc="copy_bad_params (prefix is on POSIX dst)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # (4) Bad parameter: UUID, UNS, or POSIX path does not exist.
        # These tests use parameters that do not exist."""
        self.set_datamover_params(
            "DAOS_UUID", "/", pool1, cont1,
            "POSIX", self.posix_test_paths[0])
        self.dcp_cmd.daos_src_pool.update(str(self.gen_uuid()))
        self.run_datamover(
            test_desc="copy_bad_params (src pool uuid does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS)

        self.set_datamover_params(
            "DAOS_UUID", "/", pool1, cont1,
            "POSIX", self.posix_test_paths[0])
        self.dcp_cmd.daos_src_cont.update(str(self.gen_uuid()))
        self.run_datamover(
            test_desc="copy_bad_params (src cont uuid does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS)

        self.set_datamover_params(
            "POSIX", self.posix_test_paths[0], None, None,
            "DAOS_UUID", "/", pool1, cont1)
        self.dcp_cmd.daos_dst_pool.update(str(self.gen_uuid()))
        self.run_datamover(
            test_desc="copy_bad_params (dst pool uuid does not exist)",
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS)

        self.run_datamover(
            "copy_bad_params (src cont path does not exist)",
            "DAOS_UUID", "/fake/fake", pool1, cont1,
            "POSIX", self.posix_test_paths[0],
            expected_rc=1,
            expected_output=self.MFU_ERR_INVAL_ARG)

        self.run_datamover(
            "copy_bad_params (dst cont path does not exist)",
            "POSIX", self.posix_test_paths[0], None, None,
            "DAOS_UUID", "/fake/fake", pool1, cont1,
            expected_rc=1,
            expected_output=self.MFU_ERR_INVAL_ARG)

        self.run_datamover(
            "copy_bad_params (src posix path does not exist)",
            "POSIX", "/fake/fake/fake", None, None,
            "DAOS_UUID", "/", pool1, cont1,
            expected_rc=1,
            expected_output=self.MFU_ERR_INVAL_ARG)

        self.run_datamover(
            "copy_bad_params (dst posix path does not exist)",
            "DAOS_UUID", "/", pool1, cont1,
            "POSIX", "/fake/fake/fake",
            expected_rc=1,
            expected_output=self.MFU_ERR_INVAL_ARG)

    @skipForTicket("DAOS-6871")
    def test_copy_space_dcp(self):
        """Jira ID: DAOS-5515
        Test Description:
            DAOS-5515: destination pool does not have enough space.
            DAOS-6387: posix filesystem does not have enough space.
        :avocado: tags=all,full_regression
        :avocado: tags=datamover,dcp
        :avocado: tags=copy_negative,copy_space_dcp
        """
        self.set_tool("DCP")

        # Create a large test file in POSIX
        block_size_large = self.params.get(
            "block_size_large", "/run/ior/*")
        self.ior_cmd.block_size.update(block_size_large)
        self.run_ior_with_params("POSIX", self.posix_test_file)

        # Create destination test pool and container
        pool1 = self.create_pool()
        cont1 = self.create_cont(pool1)

        # Try to copy, and expect a proper error message.
        self.run_datamover(
            self.test_id + " (dst pool out of space)",
            "POSIX", self.posix_test_paths[0], None, None,
            "DAOS_UUID", "/", pool1, cont1,
            expected_rc=1,
            expected_output=[self.MFU_ERR_DCP_COPY, "errno=28"])

        # Create another pool and container
        pool2 = self.create_pool()
        cont2 = self.create_cont(pool2)

        # Start dfuse on pool2/cont2
        self.start_dfuse(self.dfuse_hosts, pool2, cont2)

        # Try to copy. For now, we expect this to just abort.
        self.run_datamover(
            self.test_id + " (dst posix out of space)",
            "POSIX", self.posix_test_paths[0], None, None,
            "POSIX", self.dfuse.mount_dir.value,
            expected_rc=255,
            expected_err=["errno=28"])

    def test_copy_error_check_dcp(self):
        """Jira ID: DAOS-5515
        Test Description:
            Tests POSIX copy error checking for dcp.
            Tests the following cases:
                destination filename is invalid.
        :avocado: tags=all,full_regression
        :avocado: tags=datamover,dcp
        :avocado: tags=copy_negative,copy_error_check_dcp
        """
        self.set_tool("DCP")

        # Create pool and containers
        pool1 = self.create_pool()
        cont1 = self.create_cont(pool1)

        # Create source file
        self.run_ior_with_params("DAOS_UUID", self.daos_test_file,
                                 pool1, cont1)

        # Use a really long filename
        dst_path = join(self.posix_test_paths[0], "d"*300)
        self.run_datamover(
            "copy_error_check (filename is too long)",
            "DAOS_UUID", "/", pool1, cont1,
            "POSIX", dst_path,
            expected_rc=1,
            expected_output=[self.MFU_ERR_DCP_COPY, "errno=36"])

'''
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''

import uuid
from os.path import join

from data_mover_test_base import DataMoverTestBase
from dfuse_utils import get_dfuse, start_dfuse
from duns_utils import format_path


class DmvrNegativeTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Test class for POSIX DataMover negative testing.

    Test Class Description:
        Tests the following cases:
            Bad parameters.
            Simple error checking.
    :avocado: recursive
    """

    # DCP error codes
    MFU_ERR_DCP_COPY = "MFU_ERR(-1101)"
    MFU_ERR_DAOS_INVAL_ARG = "MFU_ERR(-4001)"

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the parameters
        test_file = self.ior_cmd.test_file.value

        # Setup the directory structures
        self.__posix_test_path = self.new_posix_test_path()
        self.__posix_test_file = join(self.__posix_test_path, test_file)
        self.__daos_test_path = "/"
        self.__daos_test_file = join(self.__daos_test_path, test_file)

    def test_dm_bad_params_dcp(self):
        """Jira ID: DAOS-5515 - Initial test case.
           Jira ID: DAOS-6355 - Test case reworked.
           Jira ID: DAOS-9874 - daos-prefix removed.
        Test Description:
            Test POSIX copy with invalid parameters.
            This uses the dcp tool.
            (1) Bad parameter: required argument.
            (2) Bad parameter: source is destination.
            (3) Bad parameter: UUID, UNS, or POSIX path is invalid.
            (4) Bad parameter: destination filename is invalid.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp,dfuse,dfs,ior
        :avocado: tags=DmvrNegativeTest,test_dm_bad_params_dcp
        """
        self.set_tool("DCP")

        # Start dfuse to hold all pools/containers
        dfuse = get_dfuse(self, self.dfuse_hosts)
        start_dfuse(self, dfuse)

        # Create a test pool
        pool1 = self.get_pool()

        # Create a special container to hold UNS entries
        uns_cont = self.get_container(pool1)

        # Create a test container
        cont1_path = join(dfuse.mount_dir.value, pool1.uuid, uns_cont.uuid, 'uns1')
        cont1 = self.get_container(pool1, path=cont1_path)

        # Create test files
        self.run_ior_with_params("POSIX", self.__posix_test_file)
        self.run_ior_with_params("DAOS_UUID", self.__daos_test_file, pool1, cont1)

        # Bad parameter: required arguments.
        self.run_datamover(
            self.test_id + " (missing source pool)",
            src_path=format_path(),
            dst_path=self.__posix_test_path,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            self.test_id + " (missing source cont)",
            src_path=format_path(pool1),
            dst_path=self.__posix_test_path,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            self.test_id + " (missing dest pool)",
            src_path=self.__posix_test_path,
            dst_path=format_path(),
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # (2) Bad parameter: source is destination.
        self.run_datamover(
            self.test_id + " (UUID source is UUID dest)",
            src_path=format_path(pool1.uuid, cont1.uuid),
            dst_path=format_path(pool1.uuid, cont1.uuid),
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            self.test_id + " (UNS source is UNS dest)",
            src_path=cont1.path.value,
            dst_path=cont1.path.value,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            self.test_id + " (UUID source is UNS dest)",
            src_path=format_path(pool1.uuid, cont1.uuid),
            dst_path=cont1.path.value,
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        self.run_datamover(
            self.test_id + " (UNS source is UUID dest)",
            src_path=cont1.path.value,
            dst_path=format_path(pool1.uuid, cont1.uuid),
            expected_rc=1,
            expected_output=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3) Bad parameter: UUID, UNS, or POSIX path does not exist.
        fake_uuid = str(uuid.UUID(int=0))
        self.run_datamover(
            self.test_id + " (invalid source pool)",
            src_path=format_path(fake_uuid, cont1),
            dst_path=self.__posix_test_path,
            expected_rc=1,
            expected_output="DER_NONEXIST")

        self.run_datamover(
            self.test_id + " (invalid source cont)",
            src_path=format_path(pool1, fake_uuid),
            dst_path=self.__posix_test_path,
            expected_rc=1,
            expected_output="DER_NONEXIST")

        self.run_datamover(
            self.test_id + " (invalid dest pool)",
            src_path=self.__posix_test_path,
            dst_path=format_path(fake_uuid, cont1),
            expected_rc=1,
            expected_output="DER_NONEXIST")

        self.run_datamover(
            self.test_id + " (invalid source cont path)",
            src_path=format_path(pool1, cont1, "/fake/fake"),
            dst_path=self.__posix_test_path,
            expected_rc=1,
            expected_output="No such file or directory")

        self.run_datamover(
            self.test_id + " (invalid source cont UNS path)",
            src_path=cont1.path.value + "/fake/fake",
            dst_path=self.__posix_test_path,
            expected_rc=1,
            expected_output="No such file or directory")

        self.run_datamover(
            self.test_id + " (invalid dest cont path)",
            src_path=self.__posix_test_path,
            dst_path=format_path(pool1, cont1, "/fake/fake"),
            expected_rc=1,
            expected_output="No such file or directory")

        self.run_datamover(
            self.test_id + " (invalid source posix path)",
            src_path="/fake/fake",
            dst_path=format_path(pool1, cont1),
            expected_rc=1,
            expected_output="No such file or directory")

        self.run_datamover(
            self.test_id + " (invalid dest posix path)",
            src_path=format_path(pool1, cont1),
            dst_path="/fake/fake",
            expected_rc=1,
            expected_output="No such file or directory")

        #  (4) Bad parameter: destination filename is invalid.
        dst_path = join(self.__posix_test_path, "d" * 300)
        self.run_datamover(
            self.test_id + " (filename is too long)",
            src_path=format_path(pool1, cont1),
            dst_path=dst_path,
            expected_rc=1,
            expected_output=[self.MFU_ERR_DCP_COPY, "errno=36"])

    def test_dm_bad_params_fs_copy(self):
        """Jira ID: DAOS-15388
        Test Description:
            Test POSIX copy with invalid parameters.
            This uses the dcp tool.
            (1) Bad parameter: source is destination.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,daos_fs_copy
        :avocado: tags=DmvrNegativeTest,test_dm_bad_params_fs_copy
        """
        self.set_tool("FS_COPY")

        # Start dfuse to hold all pools/containers
        dfuse = get_dfuse(self, self.dfuse_hosts)
        start_dfuse(self, dfuse)

        # Create a test pool
        pool1 = self.get_pool()

        # Create a special container to hold UNS entries
        uns_cont = self.get_container(pool1)

        # Create a test container
        cont1_path = join(dfuse.mount_dir.value, pool1.uuid, uns_cont.uuid, 'uns1')
        cont1 = self.get_container(pool1, path=cont1_path)

        # Create test files
        self.run_ior_with_params("DAOS", self.__daos_test_file, pool1, cont1)

        # (1) Bad parameter: source is destination.
        self.log_step("Verify error when label source is label dest")
        self.run_datamover(
            src_path=format_path(pool1.label.value, cont1.label.value),
            dst_path=format_path(pool1.label.value, cont1.label.value),
            expected_rc=1,
            expected_err="DER_INVAL")

        self.log_step("Verify error when UNS source is UNS dest")
        self.run_datamover(
            src_path=cont1.path.value,
            dst_path=cont1.path.value,
            expected_rc=1,
            expected_err="DER_INVAL")

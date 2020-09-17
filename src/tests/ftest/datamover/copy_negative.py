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
from command_utils import CommandFailure
from ior_test_base import IorTestBase
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from data_mover_utils import DataMover
from apricot import skipForTicket
import os
import re
import uuid

class CopyNegativeTest(IorTestBase):
    """
    Test Class Description:
        Negative testing for the Data Mover.
        Tests the following cases:
            Bad parameters.
            Simple error checking.
    :avocado: recursive
    """

    """DCP error codes"""
    MFU_ERR = -1000
    MFU_ERR_INVAL_ARG = -1001
    MFU_ERR_DCP = -1100
    MFU_ERR_DCP_COPY = -1101
    MFU_ERR_DAOS = -4000
    MFU_ERR_DAOS_INVAL_ARG = -4001

    def __init__(self, *args, **kwargs):
       	"""Initialize a CopyTypesTest object."""
        super(CopyNegativeTest, self).__init__(*args, **kwargs)
        self.containers = []
        self.pools = []
        self.pool = None
        self.uuids = []

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyNegativeTest, self).setUp()

        # Get the parameters
        self.flags_write = self.params.get("flags_write", "/run/ior/copy_negative/*")
        self.block_size = self.params.get("block_size", "/run/ior/*")
        self.block_size_large = self.params.get("block_size_large", "/run/ior/copy_negative/*")
        self.test_file = self.params.get("test_file", "/run/ior/copy_negative/*")
        self.uns_dir = self.params.get("uns_dir", "/run/container/copy_negative/*")

        # Setup the directory structures
        self.posix_test_path = os.path.join(self.tmp, "posix_test") + os.path.sep
        self.posix_test_file = os.path.join(self.posix_test_path, self.test_file)
        self.daos_test_path = "/"
        self.daos_test_file = os.path.join(self.daos_test_path, self.test_file)

        # Create the directories
        cmd = "mkdir -p '{}' '{}'".format(
            self.uns_dir,
            self.posix_test_path)
        self.execute_cmd(cmd)

    def tearDown(self):
        """Tear down each test case."""
        # Remove the created directories
        cmd = "rm -r '{}' '{}'".format(
            self.uns_dir,
            self.posix_test_path)
        self.execute_cmd(cmd)

        # Stop the servers and agents
        super(CopyNegativeTest, self).tearDown()

    def create_pool(self):
        """Create a TestPool object."""
        # Get the pool params
        pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        pool.get_params(self)

        # Create a pool
        pool.create()

        # Save the pool
        self.pools.append(pool)
        self.pool = self.pools[0]

        # Save the uuid
        self.uuids.append(str(pool.uuid))

        return pool

    def create_cont(self, pool, path=None):
        """Create a TestContainer object."""
        # Get container params
        container = TestContainer(
            pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)

        if path is not None:
            container.path.update(path)

        # Create container
        container.create()

        # Save the container
        self.containers.append(container)

        # Save the uuid
        self.uuids.append(str(container.uuid))

        return container

    def gen_uuid(self):
        """Generate a unique uuid"""
        new_uuid = str(uuid.uuid4())
        while new_uuid in self.uuids:
            new_uuid = str(uuid.uuid4())
        return new_uuid

    def test_copy_bad_params(self):
        """Jira ID: DAOS-5515
        Test Description:
            (1) Bad parameter: required argument
            (2) Bad parameter: source is destination.
            (3) Bad parameter: daos-prefix is invalid.
            (4) Bad parameter: UUID, UNS, or POSIX path is invalid.
        :avocado: tags=all,datamover
        :avocado: tags=copy_negative,copy_bad_params
        """
        # Create pool and containers
        pool1 = self.create_pool()
        pool2 = self.create_pool()
        uns1 = os.path.join(self.uns_dir, "uns1")
        container1 = self.create_cont(pool1, uns1)
        container2 = self.create_cont(pool2)

        # Bogus uuid for testing
        bogus_uuid = self.gen_uuid()

        # Create test files
        self.write_posix()
        self.write_daos(pool1, container1)

        # (1) Bad parameter: required arguments
        # (1.1) Source container but no source pool
        self.run_dcp(
            source=self.daos_test_file, target=self.posix_test_file,
            override_src_pool="", override_src_cont=container1.uuid,
            test_desc="copy_bad_params (1.1)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (1.2) Source pool but no source container
        self.run_dcp(
            source=self.daos_test_file, target=self.posix_test_file,
            override_src_pool=pool1.uuid, override_src_cont="",
            test_desc="copy_bad_params (1.2)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (1.3) Source pool but no source svcl
        self.run_dcp(
            source=self.daos_test_file, target=self.posix_test_file,
            override_src_pool=pool1.uuid, override_src_cont=container1.uuid,
            override_src_svcl="",
            test_desc="copy_bad_params (1.3)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (1.4) Destination container but no destination pool
        self.run_dcp(
            source=self.posix_test_file, target=self.daos_test_file,
            override_dst_pool="", override_dst_cont=container1.uuid,
            test_desc="copy_bad_params (1.4)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (1.5) Prefix but no source or destination svcl
        self.run_dcp(
            source=self.posix_test_file, target=uns1,
            prefix=uns1,
            override_src_svcl="", override_dst_svcl="",
            test_desc="copy_bad_params (1.5)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (1.6) Source UNS but no source svcl
        self.run_dcp(
            source=uns1, target=self.posix_test_path,
            override_src_svcl="",
            test_desc="copy_bad_params (1.6)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (1.7) Destination UNS but no destination svcl
        self.run_dcp(
            source=self.posix_test_file, target=uns1,
            override_dst_svcl="",
            test_desc="copy_bad_params (1.7)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (2) Bad parameter: source is destination
        # (2.1) UUID source is UUID destination
        self.run_dcp(
            source="/", target="/",
            src_pool=pool1, src_cont=container1,
            dst_pool=pool1, dst_cont=container1,
            test_desc="copy_bad_params (2.1)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (2.2) UNS source is UNS destination
        self.run_dcp(
            source=uns1, target=uns1,
            test_desc="copy_bad_params (2.2)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3) Bad parameter: daos-prefix is invalid
        # (3.1) Prefix is not UNS path
        fakeuns = os.path.join(self.uns_dir, "fakeuns")
        self.run_dcp(
            source=fakeuns, target=self.posix_test_path,
            prefix=fakeuns,
            test_desc="copy_bad_params (3.1)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3.2) Prefix is UNS path but doesn't match source or destination
        fakeuns = os.path.join(self.uns_dir, "fakeuns")
        self.run_dcp(
            source=fakeuns, target=self.posix_test_path,
            prefix=uns1,
            test_desc="copy_bad_params (3.2)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3.3) Prefix is UNS path but is a substring, not prefix, of source
        src = "/oops" + uns1
        self.run_dcp(
            source=src, target=self.posix_test_path,
            prefix=uns1,
            test_desc="copy_bad_params (3.3)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3.4) Prefix is UNS path but is a substring, not prefix, of destination
        dst = "/oops" + uns1
        self.run_dcp(
            source=self.posix_test_path, target=dst,
            prefix=uns1,
            test_desc="copy_bad_params (3.4)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3.7) Prefix is not UNS path but is prefix of POSIX source
        self.run_dcp(
            source=self.posix_test_path, target=uns1,
            prefix=self.posix_test_path,
            test_desc="copy_bad_params (3.7)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (3.8) Prefix is not UNS path but is prefix of POSIX destination
        self.run_dcp(
            source=uns1, target=self.posix_test_path,
            prefix=self.posix_test_path,
            test_desc="copy_bad_params (3.8)",
            expected_error_code=self.MFU_ERR_DAOS_INVAL_ARG)

        # (4) Bad parameter: UUID, UNS, or POSIX path does not exist
        # (4.1) Source pool UUID does not exist
        self.run_dcp(
            source=self.daos_test_file, target=self.posix_test_file,
            override_src_pool=bogus_uuid, override_src_cont=container1.uuid,
            test_desc="copy_bad_params (4.1)",
            expected_error_code=self.MFU_ERR_DAOS)

        # (4.2) Source pool UUID exists, source container UUID does not
        self.run_dcp(
            source=self.daos_test_file, target=self.posix_test_file,
            override_src_pool=pool1.uuid, override_src_cont=bogus_uuid,
            test_desc="copy_bad_params (4.2)",
            expected_error_code=self.MFU_ERR_DAOS)

        # (4.3) Destination pool UUID does not exist
        self.run_dcp(
            source=self.posix_test_path, target=self.daos_test_path,
            override_dst_pool=bogus_uuid, override_dst_cont=container1.uuid,
            test_desc="copy_bad_params (4.3)",
            expected_error_code=self.MFU_ERR_DAOS)

        # (4.4) Source UUIDs exist, but source path does not exist
        src = "/fake/fake/fake"
        self.run_dcp(
            source=src, target=self.posix_test_path,
            src_pool=pool1, src_cont=container1,
            test_desc="copy_bad_params (4.4)",
            expected_error_code=self.MFU_ERR_INVAL_ARG)

        # (4.5) Destination UUIDs exist, but destination path does not exist
        dst = "/fake/fake/fake"
        self.run_dcp(
            source=self.posix_test_path, target=dst,
            dst_pool=pool1, dst_cont=container1,
            test_desc="copy_bad_params (4.5)",
            expected_error_code=self.MFU_ERR_INVAL_ARG)

        # (4.6) Source POSIX path does not exist
        src = "/fake/fake/fake"
        self.run_dcp(
            source=src, target=self.daos_test_path,
            dst_pool=pool1, dst_cont=container1,
            test_desc="copy_bad_params (4.6)",
            expected_error_code=self.MFU_ERR_INVAL_ARG)

        # (4.7) Destination POSIX path does not exist
        dst = "/fake/fake/fake"
        self.run_dcp(
            source=self.daos_test_path, target=dst,
            src_pool=pool1, src_cont=container1,
            test_desc="copy_bad_params (4.7)",
            expected_error_code=self.MFU_ERR_INVAL_ARG)

    def test_copy_error_check(self):
        """Jira ID: DAOS-5515
        Test Description:
            (1) Error checking: destination filename is invalid.
            (2) Error checking: destination pool out of space.
            (3) Error checking: destination POSIX file system out of space.
        :avocado: tags=all,datamover
        :avocado: tags=copy_negative,copy_error_check
        """
        # Create pool and containers
        pool = self.create_pool()
        container1 = self.create_cont(pool)

        # (1) Destination filename is invalid
        # Create test file
        self.write_daos(pool, container1)
        # Use a really long filename
        dst_filename = "d"*300
        dst = os.path.join(self.posix_test_path, dst_filename)
        self.run_dcp(
            source=self.daos_test_file, target=dst,
            src_pool=pool, src_cont=container1,
            test_desc="error_check (1)",
            expected_error_code=self.MFU_ERR_DCP_COPY,
            expected_errno=36) # File name too long

        # (2) Destination pool out of space.
        # Write a large file to POSIX
        self.ior_cmd.block_size.update(self.block_size_large)
        self.write_posix()
        self.run_dcp(
            source=self.posix_test_file, target=self.daos_test_file,
            dst_pool=pool, dst_cont=container1,
            test_desc="error_check (2)",
            expected_error_code=self.MFU_ERR_DCP_COPY,
            expected_errno=28) # No space left on device

        # TODO: how to limit the space?
        # (3) Destination POSIX file system out of space.

    def write_daos(self, pool, container):
        """Uses ior to write the test file to a DAOS container."""
        self.ior_cmd.api.update("DFS")
        self.ior_cmd.flags.update(self.flags_write)
        self.ior_cmd.test_file.update(self.daos_test_file)
        self.ior_cmd.set_daos_params(self.server_group, pool, container.uuid)
        out = self.run_ior(self.get_ior_job_manager_command(), self.processes)

    def write_posix(self, test_file=None):
        """Uses ior to write the test file in POSIX."""
        self.ior_cmd.api.update("POSIX")
        self.ior_cmd.flags.update(self.flags_write)
        if test_file is None:
            self.ior_cmd.test_file.update(self.posix_test_file)
        else:
            self.ior_cmd.test_file.update(test_file)
        self.ior_cmd.set_daos_params(self.server_group, self.pool)
        out = self.run_ior(self.get_ior_job_manager_command(), self.processes)

    def nvl(self, s, null_val=None):
        """ Returns null_val if s is None or empty.
            Else, returns s"""
        if s is None:
            return null_val
        if not str(s):
            return null_val
        return s

    def extract_rc(self, s):
        """Extracts the rc from a CommandFailure error"""
        rc_search = re.search(r"\(rc=([0-9]+)\)", str(s))
        if not rc_search:
            return None
        else:
            return int(rc_search.group(1))

    def run_dcp(self, source, target,
                prefix=None,
                src_pool=None, dst_pool=None, src_cont=None, dst_cont=None,
                override_src_pool=None, override_dst_pool=None,
                override_src_cont=None, override_dst_cont=None,
                override_src_svcl=None, override_dst_svcl=None,
                test_desc=None,
                expected_error_code=None, expected_errno=None):
        """Use mpirun to execute the dcp utility"""
        # param for dcp processes
        processes = self.params.get("processes", "/run/datamover/*")

        # Set up the dcp command
        dcp = DataMover(self.hostlist_clients)
        dcp.get_params(self)
        dcp.daos_prefix.update(prefix)
        dcp.src_path.update(source)
        dcp.dest_path.update(target)
        dcp.set_datamover_params(src_pool, dst_pool, src_cont, dst_cont)

        # Handle manual overrides
        # nvl allows None to be passed with "" to override default values
        if override_src_pool is not None:
            dcp.daos_src_pool.update(self.nvl(override_src_pool))
        if override_src_cont is not None:
            dcp.daos_src_cont.update(self.nvl(override_src_cont))
        if override_dst_pool is not None:
            dcp.daos_dst_pool.update(self.nvl(override_dst_pool))
        if override_dst_cont is not None:
            dcp.daos_dst_cont.update(self.nvl(override_dst_cont))

        # TODO re-evaluate how to do this.
        #      is it even necessary anymore?
        # Will need to always override both to account for default values
        if override_src_svcl is not None:
            dcp.daos_src_svcl.update_default(None)
            dcp.daos_src_svcl.update(self.nvl(override_src_svcl))
        if override_dst_svcl is not None:
            dcp.daos_dst_svcl.update_default(None)
            dcp.daos_dst_svcl.update(self.nvl(override_dst_svcl))

        # Don't throw exceptions for expected failures
        if expected_error_code is not None:
            dcp.exit_status_exception = False

        # Run the dcp command
        if test_desc is not None:
            self.log.info("Running dcp: {}".format(test_desc))
        try:
            dcp_result = dcp.run(self.workdir, processes)
        except CommandFailure as error:
            self.log.error("DCP command failed: %s", str(error))
            self.fail("Test was expected to pass but it failed. {}\n".format(test_desc))

        # Look for expected errors
        if expected_error_code is not None:
            # dcp should return 1 if any error is encountered
            rc = dcp_result.exit_status
            if rc != 1:
                self.fail("Expected (rc=1) but got (rc={}): {}\n".format(rc, test_desc))
            expected_string = "MFU_ERR({})".format(expected_error_code)
            if expected_string not in dcp_result.stdout:
                self.fail("Expected {}: {}".format(expected_string, test_desc))

        # Look for expected "errno"
        if expected_errno is not None:
            expected_string = "errno={}".format(expected_errno)
            if expected_string not in dcp_result.stdout:
                self.fail("Expected {}: {}".format(expected_string, test_desc))

#!/usr/bin/python
"""
(C) Copyright 2018-2020 Intel Corporation.

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
from command_utils_base import CommandFailure
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from ior_test_base import IorTestBase
from data_mover_utils import DataMover
from os.path import join
import uuid


class DataMoverTestBase(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Base DataMover test class.

    Sample Use Case:
        set_ior_location_and_run("DAOS_UUID", "/testFile, pool1, cont1,
                                 flags="-w -K")
        set_src_location("DAOS_UUID", "/testFile", pool1, cont1)
        set_dst_location("POSIX", "/some/posix/path/testFile")
        set_ior_location_and_run("POSIX", "/some/posix/path/testFile",
                                 flags="-r -R")
    :avocado: recursive
    """

    # The valid parameter types for setting param locations.
    PARAM_TYPES = ("POSIX", "DAOS_UUID", "DAOS_UNS")

    def __init__(self, *args, **kwargs):
        """Initialize a DataMoverTestBase object."""
        super(DataMoverTestBase, self).__init__(*args, **kwargs)
        self.dm_cmd = None
        self.processes = None
        self.pool = []
        self.containers = []
        self.uuids = []
        self._gen_daos_path_v = 0
        self.dfuse_hosts = None

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(DataMoverTestBase, self).setUp()

        self.dfuse_hosts = self.agent_managers[0].hosts

        # Get the parameters for DataMover
        self.dm_cmd = DataMover(self.hostlist_clients, self.tmp)
        self.dm_cmd.get_params(self)
        self.processes = self.params.get("np", '/run/datamover/processes/*')

        # List of test paths to create and remove
        self.posix_test_paths = []

    def pre_tear_down(self):
        """Tear down steps to run before tearDown().

        Returns:
            list: a list of error strings to report at the end of tearDown().

        """
        error_list = []
        # Remove the created directories
        if self.posix_test_paths:
            command = "rm -rf {}".format(self.get_posix_test_path_string())
            try:
                self._execute_command(command)
            except CommandFailure as error:
                error_list.append(
                    "Error removing created directories: {}".format(error))
        return error_list

    def get_posix_test_path_list(self):
        """Get a list of quoted posix test path strings.

        Returns:
            list: a list of quoted posix test path strings

        """
        return ["'{}'".format(item) for item in self.posix_test_paths]

    def get_posix_test_path_string(self):
        """Get a string of all of the quoted posix test path strings.

        Returns:
            str: a string of all of the quoted posix test path strings

        """
        return " ".join(self.get_posix_test_path_list())

    def validate_param_type(self, param_type):
        """Validates the param_type.

        It converts param_types to upper-case and handles shorthand types.

        Args:
            param_type (str): The param_type to be validated.

        Returns:
            (str) A valid param_type
        """
        _type = str(param_type).upper()
        if _type == "DAOS":
            return "DAOS_UUID"
        if _type in self.PARAM_TYPES:
            return _type
        self.fail("Invalid param_type: {}".format(_type))

    def create_pool(self):
        """Create a TestPool object.

        Returns:
            TestPool: the created pool

        """
        # Get the pool params
        pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        pool.get_params(self)

        # Create the pool
        pool.create()

        # Save the pool and uuid
        self.pool.append(pool)
        self.uuids.append(str(pool.uuid))

        return pool

    def create_cont(self, pool, use_dfuse_uns=False,
                    dfuse_uns_pool=None, dfuse_uns_cont=None):
        # pylint: disable=arguments-differ
        """Create a TestContainer object.

        Args:
            pool (TestPool): pool to create the container in.
            use_dfuse_uns (bool, optional): whether to create a
                UNS path in the dfuse mount.
                Default is False.
            dfuse_uns_pool (TestPool, optional): pool in the
                dfuse mount for which to create a UNS path.
                Default assumes dfuse is running for a specific pool.
            dfuse_uns_cont (TestContainer, optional): container in the
                dfuse mount for which to create a UNS path.
                Default assumes dfuse is running for a specific container.

        Returns:
            The TestContainer

        Note about uns path:
            These are only created within a dfuse mount.
            The full UNS path will be created as:
            <dfuse.mount_dir>/[pool_uuid]/[cont_uuid]/<dir_name>
            dfuse_uns_pool and dfuse_uns_cont should only be supplied
            when dfuse was not started for a specific pool/container.

        """
        # Get container params
        container = TestContainer(
            pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)

        if use_dfuse_uns:
            path = str(self.dfuse.mount_dir.value)
            if dfuse_uns_pool:
                path = join(path, dfuse_uns_pool.uuid)
            if dfuse_uns_cont:
                path = join(path, dfuse_uns_cont.uuid)
            path = join(path, "uns{}".format(str(len(self.containers))))
            container.path.update(path)

        # Create container
        container.create()

        # Save container and uuid
        self.containers.append(container)
        self.uuids.append(str(container.uuid))

        return container

    def gen_uuid(self):
        """Generate a unique uuid."""
        new_uuid = str(uuid.uuid4())
        while new_uuid in self.uuids:
            new_uuid = str(uuid.uuid4())
        return new_uuid

    def gen_daos_path(self, prefix=None):
        """Returns the next unique container path.

        Args:
            prefix (str, optional): Path to prepend to the
                beginning of the new path.
                I.e. <prefix>/unique_dir
        """
        daos_dir = "dir{}".format(str(self._gen_daos_path_v))
        self._gen_daos_path_v += 1
        if prefix:
            return join(prefix, daos_dir)
        return join("/", daos_dir)

    @staticmethod
    def svcl_from_pool(pool):
        """Get the string svc for a pool."""
        if not hasattr(pool, "svc_ranks"):
            return None
        return ":".join(map(str, pool.svc_ranks))

    def set_src_location(self, *args, **kwargs):
        """Shorthand for set_location("src", ...)."""
        self.set_location("src", *args, **kwargs)

    def set_dst_location(self, *args, **kwargs):
        """Shorthand for set_location("dst", ...)."""
        self.set_location("dst", *args, **kwargs)

    def set_location(self, src_or_dst, param_type, path,
                     pool=None, cont=None, display=True):
        """Set the src or dst params based on the location.

        Args:
            src_or_dst (str): set params for src or dst
            param_type (str): how to interpret the location
            path (str): posix-style path.
                For containers, this is relative to the container root
            pool (TestPool, optional): the pool object.
                Alternatively, this can the pool uuid,
                which ignores other pool attributes
            cont (TestContainer, optional): the container object.
                Alternatively, this can be the container uuid,
                which ignores other container attributes
            display (bool, optional): print updated params. Defaults to True.
        """
        assert src_or_dst in ["src", "dst"] # nosec
        param_type = self.validate_param_type(param_type)

        # Get refs to either src or dst
        if src_or_dst == "src":
            dm_path = self.dm_cmd.src_path
            dm_pool = self.dm_cmd.daos_src_pool
            dm_svcl = self.dm_cmd.daos_src_svcl
            dm_cont = self.dm_cmd.daos_src_cont
            display_path = "src_path" if display else None
            display_pool = "daos_src_pool" if display else None
            display_svcl = "daos_src_svcl" if display else None
            display_cont = "daos_src_cont" if display else None
        elif src_or_dst == "dst":
            dm_path = self.dm_cmd.dest_path
            dm_pool = self.dm_cmd.daos_dst_pool
            dm_svcl = self.dm_cmd.daos_dst_svcl
            dm_cont = self.dm_cmd.daos_dst_cont
            display_path = "dest_path" if display else None
            display_pool = "daos_dst_pool" if display else None
            display_svcl = "daos_dst_svcl" if display else None
            display_cont = "daos_dst_cont" if display else None

        # Only a single prefix supported at this time.
        # When needing to use a prefix, the last call
        # to this function will determine the prefix value.
        dm_prefix = self.dm_cmd.daos_prefix
        display_prefix = "daos_prefix" if display else None

        # Reset params
        dm_path.update(None)
        dm_pool.update(None)
        dm_svcl.update(None)
        dm_cont.update(None)
        dm_prefix.update(None)

        # Allow cont to be either the container or the uuid
        cont_uuid = cont.uuid if hasattr(cont, "uuid") else cont

        # Allow pool to be either the pool or the uuid
        if hasattr(pool, "uuid"):
            pool_uuid = pool.uuid
            pool_svcl = self.svcl_from_pool(pool)
        else:
            pool_uuid = pool
            pool_svcl = None

        if param_type == "POSIX":
            dm_path.update(path, display_path)
        elif param_type == "DAOS_UUID":
            dm_path.update(path, display_path)
            if pool_uuid:
                dm_pool.update(pool_uuid, display_pool)
            if pool_svcl:
                dm_svcl.update(pool_svcl, display_svcl)
            if cont_uuid:
                dm_cont.update(cont_uuid, display_cont)
        elif param_type == "DAOS_UNS":
            if cont:
                if path == "/":
                    dm_path.update(cont.path, display_path)
                else:
                    dm_prefix.update(cont.path, display_prefix)
                    dm_path.update(str(cont.path) + path, display_path)
            if pool_svcl:
                dm_svcl.update(pool_svcl, display_svcl)

    def set_ior_location(self, param_type, path, pool=None, cont=None,
                         path_suffix=None, display=True):
        """Set the ior params based on the location.

        Args:
            param_type (str): how to interpret the location
            path (str): posix-style path.
                For containers, this is relative to the container root
            pool (TestPool, optional): the pool object
            cont (TestContainer, optional): the container object.
                Alternatively, this can be the container uuid
            path_suffix (str, optional): suffix to append to the path.
                E.g. path="/some/path", path_suffix="testFile"
            display (bool, optional): print updated params. Defaults to True.
        """
        param_type = self.validate_param_type(param_type)

        # Reset params
        self.ior_cmd.api.update(None)
        self.ior_cmd.test_file.update(None)
        self.ior_cmd.dfs_pool.update(None)
        self.ior_cmd.dfs_cont.update(None)
        self.ior_cmd.dfs_group.update(None)
        self.ior_cmd.dfs_svcl.update(None)

        display_api = "api" if display else None
        display_test_file = "test_file" if display else None

        # Allow cont to be either the container or the uuid
        cont_uuid = cont.uuid if hasattr(cont, "uuid") else cont

        # Optionally append suffix
        if path_suffix:
            if path_suffix[0] == "/":
                path_suffix = path_suffix[1:]
            path = join(path, path_suffix)

        if param_type == "POSIX":
            self.ior_cmd.api.update("POSIX", display_api)
            self.ior_cmd.test_file.update(path, display_test_file)
        elif param_type in ("DAOS_UUID", "DAOS_UNS"):
            self.ior_cmd.api.update("DFS", display_api)
            self.ior_cmd.test_file.update(path, display_test_file)
            if pool and cont_uuid:
                self.ior_cmd.set_daos_params(self.server_group,
                                             pool, cont_uuid)
            elif pool:
                self.ior_cmd.set_daos_params(self.server_group,
                                             pool, None)

    def set_ior_location_and_run(self, param_type, path, pool=None, cont=None,
                                 path_suffix=None, flags=None, display=True):
        """Set the ior params based on the location and run ior with some flags.

        Args:
            param_type: see set_ior_location
            path: see set_ior location
            pool: see set_ior location
            cont: see set_ior location
            path_suffix: see set_ior location
            flags (str, optional): ior_cmd flags to set
            display (bool, optional): print updated params. Defaults to True.
        """
        self.set_ior_location(param_type, path, pool, cont, path_suffix)
        if flags:
            self.ior_cmd.flags.update(flags, "flags" if display else None)
        self.run_ior(self.get_ior_job_manager_command(), self.processes,
                     display_space=(True if pool else False), pool=pool)

    def run_datamover(self, test_desc=None,
                      expected_rc=0, expected_output=None,
                      processes=None):
        """Run the DataMover command.

        Currently, this only uses "dcp".

        Args:
            test_desc (str, optional): description to print before running
            expected_rc (int, optional): rc expected to be returned
            expected_output (list, optional): substrings expected to be output
            processes (int, optional): number of mpi processes.
                defaults to self.processes

        Returns:
            The result "run" object

        """
        if not processes:
            processes = self.processes

        # Default expected_output to empty list
        if not expected_output:
            expected_output = []

        # Convert singular value to list
        if not isinstance(expected_output, list):
            expected_output = [expected_output]

        if test_desc is not None:
            self.log.info("Running DCP: %s", test_desc)

        # If we expect an rc other than 0, don't fail
        self.dm_cmd.exit_status_exception = (expected_rc == 0)

        try:
            result = self.dm_cmd.run(self.workdir, processes)
        except CommandFailure as error:
            self.log.error("DCP command failed: %s", str(error))
            self.fail("Test was expected to pass but it failed: {}\n".format(
                test_desc))

        # Check the return code
        actual_rc = result.exit_status
        if actual_rc != expected_rc:
            self.fail("Expected (rc={}) but got (rc={}): {}\n".format(
                expected_rc, actual_rc, test_desc))

        # Check for expected output
        for s in expected_output:
            if s not in result.stdout:
                self.fail("Expected {}: {}".format(s, test_desc))

        return result

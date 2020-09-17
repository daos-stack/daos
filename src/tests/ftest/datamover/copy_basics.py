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
from dfuse_test_base import DfuseTestBase
from daos_utils import DaosCommand
from test_utils_pool import TestPool
from test_utils_container import TestContainer
from data_mover_utils import DataMover
from os.path import join, sep
import uuid

# pylint: disable=too-many-ancestors
class CopyBasicsTest(IorTestBase, DfuseTestBase):
    """ Test Class Description:
        Tests basic functionality of the datamover utility.
        Tests the following cases:
            Copying between UUIDs, UNS paths, and external POSIX systems.
            Copying between pools.
    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a CopyBasicsTest object."""
        super(CopyBasicsTest, self).__init__(*args, **kwargs)
        self.containers = []
        self.pools = []
        self.pool = None
        self.uuids = []

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super(CopyBasicsTest, self).setUp()

        # Get the parameters
        self.flags_write = self.params.get(
            "flags_write", "/run/ior/copy_basics/*")
        self.flags_read = self.params.get(
            "flags_read", "/run/ior/copy_basics/*")
        self.test_file = self.params.get(
            "test_file", "/run/ior/copy_basics/*")

        # Setup the directory structures
        self.posix_test_path = join(self.tmp, "posix_test") + sep
        self.posix_test_path2 = join(self.tmp, "posix_test2") + sep

        # Create the directories
        cmd = "mkdir -p '{}' '{}'".format(
            self.posix_test_path,
            self.posix_test_path2)
        self.execute_cmd(cmd)

    def tearDown(self):
        """Tear down each test case."""
        # Remove the created directories
        cmd = "rm -rf '{}' '{}'".format(
            self.posix_test_path,
            self.posix_test_path2)
        self.execute_cmd(cmd)

        # Stop the servers and agents
        super(CopyBasicsTest, self).tearDown()

    def _create_pool(self):
        """Create a TestPool object."""
        # Get the pool params
        pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        pool.get_params(self)

        # Create a pool
        pool.create()

        # Save pool
        self.pools.append(pool)
        self.pool = self.pools[0]

        # Save uuid
        self.uuids.append(str(pool.uuid))
        return pool

    def _create_cont(self, pool, uns_cont=None):
        """ Create a TestContainer object.

            Args:
                pool (TestPool): pool object
                uns_cont (TestContainer, optional): container for uns paths
        """
        # Get container params
        container = TestContainer(
            pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)

        cont_num = len(self.containers)

        if uns_cont:
            path = join(self.dfuse.mount_dir.value, str(uns_cont.pool.uuid))
            path = join(path, str(uns_cont.uuid))
            path = join(path, str(cont_num))
            container.path.update(path)

        # Create container
        container.create()

        # Save container
        self.containers.append(container)

        # Save uuid
        self.uuids.append(str(container.uuid))

        return container

    def gen_uuid(self):
        """Generate a unique uuid"""
        new_uuid = str(uuid.uuid4())
        while new_uuid in self.uuids:
            new_uuid = str(uuid.uuid4())
        return new_uuid

    def test_copy_types(self):
        """
        Test Description:
            DAOS-5508: Verify copy between POSIX, UUIDs, and UNS paths
            Daos-5511: Verify copy across pools.
        Use Cases:
            Create pool1 and pool2.
            Create POSIX type cont1 and cont2 in pool1 with UNS paths.
            Create POSIX type cont3 in pool2 with a UNS path.
            Create a single 1K file in cont1 using ior.
            Copy all data from cont1 (UUIDs) to cont2 (UUIDs).
            Copy all data from cont1 (UUIDs) to cont2 (UNS).
            Copy all data form cont1 (UUIDs) to cont3 (UUIDs).
            Copy all data from cont1 (UUIDs) to cont3 (UNS).
            Copy all data from cont1 (UUIDs) to an external POSIX FS.
            Copy all data from cont1 (UNS) to cont2 (UUIDs).
            Copy all data from cont1 (UNS) to cont2 (UNS).
            Copy all data from cont1 (UNS) to cont3 (UUIDs).
            Copy all data from cont1 (UNS) to cont3 (UNS).
            Copy all data from cont1 (UNS) to an external POSIX FS.
            Create a single 1K file in the external POSIX using ior.
            Copy all data from POSIX to cont2 (UUIDs).
            Copy all data from POSIX to cont2 (UNS).
            Copy all data from POSIX FS to a different POSIX FS.
        :avocado: tags=all,pr,datamover
        :avocado: tags=copy_basics,copy_types
        """
        # Start dfuse to hold all pools/containers
        self.start_dfuse(self.hostlist_clients)

        # Create 2 pools
        pool1 = self._create_pool()
        pool2 = self._create_pool()

        # Create a special container to hold UNS entries
        uns_cont = self._create_cont(pool1)

        # Create all other containers
        container1 = self._create_cont(pool1, uns_cont=uns_cont)
        container2 = self._create_cont(pool1, uns_cont=uns_cont)
        container3 = self._create_cont(pool2, uns_cont=uns_cont)

        # Create each source location
        p1_c1 = DataMoverLocation("POSIX_CONT", "/", pool1, container1)
        posix1 = DataMoverLocation("POSIX_FS", self.posix_test_path)

        # Create each destination location
        p1_c2 = DataMoverLocation("POSIX_CONT", "/", pool1, container2)
        p2_c3 = DataMoverLocation("POSIX_CONT", "/", pool2, container3)
        posix2 = DataMoverLocation("POSIX_FS", self.posix_test_path2)

        # Create the source files
        self.write_location(p1_c1)
        self.write_location(posix1)

        # Make a list of each test case to run
        # [[test_desc, src_loc, src_dcp_type, dst_loc, dst_dcp_type]]
        copy_list = []

        copy_list.append([
            "DCP: UUID -> UUID (same pool)",
            p1_c1, "DAOS_UUID", p1_c2, "DAOS_UUID"])

        copy_list.append([
            "DCP: UUID -> UUID (different pool)",
            p1_c1, "DAOS_UUID", p2_c3, "DAOS_UUID"])

        copy_list.append([
            "DCP: UUID -> UNS (same pool)",
            p1_c1, "DAOS_UUID", p1_c2, "DAOS_UNS"])

        copy_list.append([
            "DCP: UUID -> UNS (different pool)",
            p1_c1, "DAOS_UUID", p2_c3, "DAOS_UNS"])

        copy_list.append([
            "DCP: UUID -> POSIX",
            p1_c1, "DAOS_UUID", posix2, "POSIX"])

        copy_list.append([
            "DCP: UNS -> UUID (same pool)",
            p1_c1, "DAOS_UNS", p1_c2, "DAOS_UUID"])

        copy_list.append([
            "DCP: UNS -> UUID (different pool)",
            p1_c1, "DAOS_UNS", p2_c3, "DAOS_UUID"])

        copy_list.append([
            "DCP: UNS -> UNS (same pool)",
            p1_c1, "DAOS_UNS", p1_c2, "DAOS_UNS"])

        copy_list.append([
            "DCP: UNS -> UNS (different pool)",
            p1_c1, "DAOS_UNS", p2_c3, "DAOS_UNS"])

        copy_list.append([
            "DCP: UNS -> POSIX",
            p1_c1, "DAOS_UNS", posix2, "POSIX"])

        copy_list.append([
            "DCP: POSIX -> UUID",
            posix1, "POSIX", p1_c2, "DAOS_UUID"])

        copy_list.append([
            "DCP: POSIX -> UNS",
            posix1, "POSIX", p1_c2, "DAOS_UNS"])

        copy_list.append([
            "DCP: POSIX -> POSIX",
            posix1, "POSIX", posix2, "POSIX"])

        # Run each test
        for (test_desc, src_loc, src_dcp_type, dst_loc, dst_dcp_type) in copy_list:
            src_dcp_params = src_loc.get_dcp_params(src_dcp_type)
            dst_dcp_params = dst_loc.get_dcp_params(dst_dcp_type)
            self.run_dcp(
                src=src_dcp_params.path, dst=dst_dcp_params.path,
                src_pool=src_dcp_params.pool, src_cont=src_dcp_params.cont,
                dst_pool=dst_dcp_params.pool, dst_cont=dst_dcp_params.cont,
                src_svc=src_dcp_params.svc, dst_svc=dst_dcp_params.svc,
                test_desc=test_desc)
            self.read_verify_location(dst_loc)

    def test_copy_auto_create_dest(self):
        """
        Test Description:
            DAOS-5653: Verify auto-creation of destination container
        Use Cases:
            Create pool1 and pool2.
            Create POSIX cont1 in pool1.
            Create a single 1K file in cont1 using ior.
            Copy all data from cont1 to pool1, with a new cont UUID.
            Copy all data from cont1 to pool2, with a new cont UUID.
        :avocado: tags=all,pr,datamover
        :avocado: tags=copy_basics,copy_auto_create_dest
        """
        # Create pools and src container
        pool1 = self._create_pool()
        pool2 = self._create_pool()
        container1 = self._create_cont(pool1)

        # Create the source file
        self.setup_and_run_ior(
            "DFS", self.flags_write,
            "/", pool1, container1.uuid)

        # pool1 -> pool1
        new_uuid = self.gen_uuid()
        self.run_dcp(
            src="/", dst="/",
            src_pool=pool1, src_cont=container1,
            dst_pool=pool1,
            override_dst_cont=new_uuid,
            test_desc="copy_auto_create_dest (1)")
        self.setup_and_run_ior(
            "DFS", self.flags_read,
            "/", pool1, new_uuid)

        # pool1 -> pool2
        new_uuid = self.gen_uuid()
        self.run_dcp(
            src="/", dst="/",
            src_pool=pool1, src_cont=container1,
            dst_pool=pool2,
            override_dst_cont=new_uuid,
            test_desc="copy_auto_create_dest (2)")
        self.setup_and_run_ior(
            "DFS", self.flags_read, "/",
            pool2, new_uuid)

    def setup_and_run_ior(self, api, flags, path, pool, cont_uuid):
        """ Runs ior with some params """
        self.ior_cmd.api.update(api)
        self.ior_cmd.flags.update(flags)
        test_file = join(path, self.test_file)
        self.ior_cmd.test_file.update(test_file)
        if pool:
            self.ior_cmd.set_daos_params(self.server_group, pool, cont_uuid)
        else:
            self.ior_cmd.dfs_pool.update(None)
            self.ior_cmd.dfs_cont.update(None)
            self.ior_cmd.dfs_group.update(None)
            self.ior_cmd.dfs_svcl.update(None)
        self.run_ior(self.get_ior_job_manager_command(), self.processes)

    def write_location(self, loc):
        """ Writes the test data using ior """
        ior_params = loc.get_ior_params()
        self.setup_and_run_ior(
            ior_params.api,
            self.flags_write,
            ior_params.path,
            ior_params.pool,
            ior_params.cont_uuid)

    def read_verify_location(self, loc):
        """ Read verifies the test data using ior """
        ior_params = loc.get_ior_params()
        self.setup_and_run_ior(
            ior_params.api,
            self.flags_read,
            ior_params.path,
            ior_params.pool,
            ior_params.cont_uuid)

    # pylint: disable=too-many-arguments
    def run_dcp(self, src, dst,
                prefix=None,
                src_pool=None, dst_pool=None, src_cont=None, dst_cont=None,
                src_svc=None, dst_svc=None,
                override_dst_cont=None,
                test_desc=None):
        """Use mpirun to execute the dcp utility"""
        # param for dcp processes
        processes = self.params.get("processes", "/run/datamover/*")

        # Set up the dcp command
        dcp = DataMover(self.hostlist_clients)
        dcp.get_params(self)
        dcp.daos_prefix.update(prefix)
        dcp.src_path.update(src)
        dcp.dest_path.update(dst)
        dcp.set_datamover_params(src_pool, dst_pool, src_cont, dst_cont)

        # Set the service levels. Useful for UNS paths,
        # since set_datamover_params doesn't set this without a pool.
        if src_svc is not None:
            dcp.daos_src_svcl.update(src_svc)
        if dst_svc is not None:
            dcp.daos_dst_svcl.update(dst_svc)

        # Handle manual overrides
        if override_dst_cont is not None:
            dcp.daos_dst_cont.update(override_dst_cont)

        # Run the dcp command
        if test_desc is not None:
            self.log.info("Running dcp: %s", test_desc)
        try:
            dcp.run(self.workdir, processes)
        except CommandFailure as error:
            self.log.error("DCP command failed: %s", str(error))
            self.fail("Test was expected to pass but it failed: {}\n".format(test_desc))

class DataMoverLocation():
    """ Class defining an object that allows a "location"
        to be exported in a format suitable for multiple
        utilities and configurations.
        For example, one could instantiate a "POSIX_CONT"
        location that represents a DAOS container of type
        "POSIX". The parameters for this location could
        be exported for use with "ior" or "dcp".
    """

    def __init__(self, loc_type, path, pool=None, cont=None):
        """ Initialize a DataMoverLocation

            Args:
                loc_type (str): The location type, such as
                    POSIX_FS or POSIX_CONT
                path (str): The posix-style path. For containers,
                    this is relative to the root of the container.
                pool (TestPool): The pool object
                cont (TestContainer): The container object
        """
        assert loc_type in ["POSIX_FS", "POSIX_CONT"]
        self.loc_type = loc_type
        self.path = path
        self.pool = pool
        self.cont = cont

    def svc_from_pool(self):
        """ Get the string svc for a pool """
        return ":".join(map(str, self.pool.svc_ranks))

    class _DcpParams():
        """ Object to hold dcp params """
        def __init__(self, path=None, pool=None, cont=None, svc=None):
            self.path = path
            self.pool = pool
            self.cont = cont
            self.svc = svc

    def get_dcp_params(self, dcp_type):
        """ Export for use with "dcp"

            Args:
                dcp_type (str): Type specifying how this
                    location will be used with dcp.

            Returns:
                _DcpParams object
        """
        assert dcp_type in ["POSIX", "DAOS_UUID", "DAOS_UNS"]
        dcp_params = self._DcpParams()
        if dcp_type == "POSIX":
            assert self.loc_type == "POSIX_FS"
            dcp_params.path = self.path
        elif dcp_type == "DAOS_UUID":
            assert self.loc_type == "POSIX_CONT"
            dcp_params.path = self.path
            dcp_params.pool = self.pool
            dcp_params.cont = self.cont
            dcp_params.svc = self.svc_from_pool()
        elif dcp_type == "DAOS_UNS":
            assert self.loc_type == "POSIX_CONT"
            dcp_params.path = self.cont.path
            dcp_params.svc = self.svc_from_pool()
        return dcp_params

    class _IorParams():
        """ Object to hold ior params """
        def __init__(self, api=None, path=None, pool=None, cont_uuid=None):
            self.api = api
            self.path = path
            self.pool = pool
            self.cont_uuid = cont_uuid

    def get_ior_params(self):
        """ Export for use with "ior"

            Returns:
                _IorParams object
        """

        ior_params = self._IorParams()
        if self.loc_type == "POSIX_FS":
            assert self.loc_type == "POSIX_FS"
            ior_params.api = "POSIX"
            ior_params.path = self.path
        elif self.loc_type == "POSIX_CONT":
            assert self.loc_type == "POSIX_CONT"
            ior_params.api = "DFS"
            ior_params.path = self.path
            ior_params.pool = self.pool
            if self.cont:
                ior_params.cont_uuid = self.cont.uuid
        return ior_params

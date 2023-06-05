"""
(C) Copyright 2018-2023 Intel Corporation.

SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines
import os
from os.path import join
import re
import ctypes

from pydaos.raw import str_to_c_uuid, DaosContainer, DaosObj, IORequest

from exception_utils import CommandFailure
from test_utils_container import TestContainer
from ior_test_base import IorTestBase
from mdtest_test_base import MdtestBase
from data_mover_utils import DcpCommand, DsyncCommand, FsCopy, ContClone
from data_mover_utils import DserializeCommand, DdeserializeCommand
from data_mover_utils import uuid_from_obj
from duns_utils import format_path
from general_utils import create_string_buffer, get_log_file
from command_utils_base import BasicParameter, EnvironmentVariables


class DataMoverTestBase(IorTestBase, MdtestBase):
    # pylint: disable=too-many-instance-attributes
    """Base DataMover test class.

    Optional yaml config values:
        datamover/posix_root (str): path to POSIX filesystem.
        datamover/tool (list): default datamover tool to use.
        datamover/np (int): default processes for all tools.
        datamover/ppn (int): default processes-per-client for all tools.

    Sample Use Case:
        # Create test file
        run_ior_with_params("DAOS", "/testFile, pool1, cont1, flags="-w -K")

        # Set dcp as the tool to use
        self.set_tool("DCP")

        # Copy from DAOS to POSIX
        run_datamover(
            "some test description",
            src_path="daos://pool1/cont1/testFile",
            dst_path="/some/posix/path/testFile")

        # Verify destination file
        run_ior_with_params("POSIX", "/some/posix/path/testFile", flags="-r -R")
    :avocado: recursive

    """

    # The valid parameter types for setting params.
    PARAM_TYPES = ("POSIX", "DAOS_UUID", "DAOS_UNS")

    # The valid datamover tools that can be used
    TOOLS = (
        "DCP",        # mpifileutils dcp
        "DSYNC",      # mpifileutils dsync
        "DSERIAL",    # mpifileutils daos-serialize + daos-deserialize
        "FS_COPY",    # daos filesystem copy
        "CONT_CLONE"  # daos container clone
    )

    def __init__(self, *args, **kwargs):
        """Initialize a DataMoverTestBase object."""
        super().__init__(*args, **kwargs)
        self.tool = None
        self.api = None
        self.daos_cmd = None
        self.dcp_cmd = None
        self.dsync_cmd = None
        self.dserialize_cmd = None
        self.ddeserialize_cmd = None
        self.fs_copy_cmd = None
        self.cont_clone_cmd = None
        self.pool = []
        self.dfuse_hosts = None
        self.num_run_datamover = 0  # Number of times run_datamover was called
        self.job_manager = None

        # Override processes and np from IorTestBase and MdtestBase to use "datamover" namespace.
        # Define processes and np for each datamover tool, which defaults to the "datamover" one.
        self.processes = None
        self.ppn = None
        self.datamover_np = None
        self.datamover_ppn = None
        self.ior_np = None
        self.ior_ppn = None
        self.mdtest_np = None
        self.mdtest_ppn = None
        self.dcp_np = None
        self.dsync_np = None
        self.dserialize_np = None
        self.ddeserialize_np = None

        # Root directory for POSIX paths. Default is self.tmp
        posix_root_map = {'self.workdir': self.workdir, 'self.tmp': self.tmp}
        self.posix_root = BasicParameter(None, mapped_values=posix_root_map)

        # Temp directory for serialize/deserialize
        self.serial_tmp_dir = self.tmp

        self.preserve_props_path = None

        # List of local test paths to create and remove
        self.posix_local_test_paths = []

        # List of shared test paths to create and remove
        self.posix_shared_test_paths = []

        # paths to unmount in teardown
        self.mounted_posix_test_paths = []

        # List of daos test paths to keep track of
        self.daos_test_paths = []

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        self.dfuse_hosts = self.agent_managers[0].hosts

        # initialize daos_cmd
        self.daos_cmd = self.get_daos_command()

        # Get the processes and np for all datamover tools, as well as for individual tools.
        self.datamover_np = self.params.get("np", '/run/datamover/*', 1)
        self.datamover_ppn = self.params.get("ppn", '/run/datamover/*', 1)
        self.ior_np = self.params.get("np", '/run/ior/client_processes/*', 1)
        self.ior_ppn = self.params.get("ppn", '/run/ior/client_processes/*', None)
        self.mdtest_np = self.params.get("np", '/run/mdtest/client_processes/*', 1)
        self.mdtest_ppn = self.params.get("ppn", '/run/mdtest/client_processes/*', None)
        self.dcp_np = self.params.get("np", "/run/dcp/*", self.datamover_np)
        self.dcp_ppn = self.params.get("ppn", "/run/dcp/*", self.datamover_ppn)
        self.dsync_np = self.params.get("np", "/run/dsync/*", self.datamover_np)
        self.dsync_ppn = self.params.get("ppn", "/run/dsync/*", self.datamover_ppn)
        self.dserialize_np = self.params.get("np", "/run/dserialize/*", self.datamover_np)
        self.dserialize_ppn = self.params.get("ppn", "/run/dserialize/*", self.datamover_ppn)
        self.ddeserialize_np = self.params.get("np", "/run/ddeserialize/*", self.datamover_np)
        self.ddeserialize_ppn = self.params.get("ppn", "/run/ddeserialize/*", self.datamover_ppn)

        self.posix_root.update_default(self.tmp)
        self.posix_root.get_yaml_value("posix_root", self, "/run/datamover/*")

        tool = self.params.get("tool", "/run/datamover/*")
        if tool:
            self.set_tool(tool)

    def pre_tear_down(self):
        """Tear down steps to run before tearDown().

        Returns:
            list: a list of error strings to report at the end of tearDown().

        """
        # doesn't append to error list because it reports an error if all
        # processes completed successfully (nothing to stop), but this call is
        # necessary in the case that mpi processes are ran across multiple nodes
        # and a timeout occurs. If this happens then cleanup on shared posix
        # directories causes errors (because an MPI process might still have it open)
        error_list = []

        if self.job_manager:
            self.job_manager.kill()

        # cleanup mounted paths
        if self.mounted_posix_test_paths:
            path_list = self._get_posix_test_path_list(path_list=self.mounted_posix_test_paths)
            for item in path_list:
                # need to remove contents before umount
                rm_cmd = "rm -rf {}/*".format(item)
                try:
                    self._execute_command(rm_cmd)
                except CommandFailure as error:
                    error_list.append("Error removing directory contents: {}".format(error))
                umount_cmd = "sudo umount -f {}".format(item)
                try:
                    self._execute_command(umount_cmd)
                except CommandFailure as error:
                    error_list.append("Error umounting posix test directory: {}".format(error))

        # cleanup local paths
        if self.posix_local_test_paths:
            command = "rm -rf {}".format(self._get_posix_test_path_string())
            try:
                self._execute_command(command)
            except CommandFailure as error:
                error_list.append("Error removing created directories: {}".format(error))

        # cleanup shared paths (only runs on one node in job)
        if self.posix_shared_test_paths:
            shared_path_strs = self._get_posix_test_path_string(path=self.posix_shared_test_paths)
            command = "rm -rf {}".format(shared_path_strs)
            try:
                # only call rm on one client since this is cleaning up shared dir
                self._execute_command(command, hosts=list(self.hostlist_clients)[0:1])
            except CommandFailure as error:
                error_list.append("Error removing created directories: {}".format(error))
        return error_list

    def set_api(self, api):
        """Set the api.

        Args:
            api (str): the api to use.

        """
        self.api = api

    def set_tool(self, tool):
        """Set the copy tool.

        Converts to upper-case and fails if the tool is not valid.

        Args:
            tool (str): the tool to use. Must be in self.TOOLS

        """
        _tool = str(tool).upper()
        if _tool in self.TOOLS:
            self.log.info("DataMover tool = %s", _tool)
            self.tool = _tool
        else:
            self.fail("Invalid tool: {}".format(_tool))

    def _get_posix_test_path_list(self, path_list=None):
        """Get a list of quoted posix test path strings.

        Returns:
            list: a list of quoted posix test path strings

        """
        if path_list is None:
            path_list = self.posix_local_test_paths

        return ["'{}'".format(item) for item in path_list]

    def _get_posix_test_path_string(self, path=None):
        """Get a string of all of the quoted posix test path strings.

        Returns:
            str: a string of all of the quoted posix test path strings

        """
        return " ".join(self._get_posix_test_path_list(path_list=path))

    def new_posix_test_path(self, shared=False, create=True, parent=None, mount_dir=False):
        """Generate a new, unique posix path.

        Args:
            shared (bool): Whether to create a directory shared across nodes or local.
                Defaults to False.
            create (bool): Whether to create the directory.
                Defaults to True.
            mount_dir (bool): Whether or not posix directory will be manually mounted in tmpfs.
            parent (str, optional): The parent directory to create the
                path in. Defaults to self.posix_root, which defaults to self.tmp.

        Returns:
            str: the posix path.

        """
        # make directory name unique to datamover test
        method = self.get_test_name()
        dir_name = "{}{}".format(method, len(self.posix_local_test_paths))

        path = join(parent or self.posix_root.value, dir_name)

        # Add to the list of posix paths
        if shared:
            self.posix_shared_test_paths.append(path)
        else:
            self.posix_local_test_paths.append(path)

        if create:
            # Create the directory
            cmd = "mkdir -p '{}'".format(path)
            self.execute_cmd(cmd)

        # mount small tmpfs filesystem on posix path, using size required sudo
        # add mount_dir to mounted list for use when umounting
        if mount_dir:
            self.mounted_posix_test_paths.append(path)
            self.execute_cmd("sudo mount -t tmpfs none '{}' -o size=128M".format(path))

        return path

    def new_daos_test_path(self, create=True, cont=None, parent="/"):
        """Create a new, unique daos container path.

        Args:
            create (bool, optional): Whether to create the directory.
                Defaults to True.
            cont (TestContainer, optional): The container to create the
                path within. This container should have a UNS path in DFUSE.
            parent (str, optional): The parent directory relative to the
                container root. Defaults to "/".

        Returns:
            str: the path relative to the root of the container.

        """
        dir_name = "daos_test{}".format(len(self.daos_test_paths))
        path = join(parent, dir_name)

        # Add to the list of daos paths
        self.daos_test_paths.append(path)

        if create:
            if not cont or not cont.path:
                self.fail("Container path required to create directory.")
            # Create the directory relative to the container path
            cmd = "mkdir -p '{}'".format(cont.path.value + path)
            self.execute_cmd(cmd)

        return path

    def _validate_param_type(self, param_type):
        """Validates the param_type.

        It converts param_types to upper-case and handles shorthand types.

        Args:
            param_type (str): The param_type to be validated.

        Returns:
            str: A valid param_type

        """
        _type = str(param_type).upper()
        if _type == "DAOS":
            return "DAOS_UUID"
        if _type in self.PARAM_TYPES:
            return _type
        self.fail("Invalid param_type: {}".format(_type))
        return None

    def create_pool(self, **params):
        """Create a TestPool object and adds to self.pool.

        Returns:
            TestPool: the created pool

        """
        pool = self.get_pool(connect=False, **params)

        # Save the pool
        self.pool.append(pool)

        return pool

    def get_cont(self, pool, cont):
        """Get an existing container.

        Args:
            pool (TestPool): pool to open the container in.
            cont (str): container uuid or label.

        Returns:
            TestContainer: the container object

        """
        # Query the container for existence and to get the uuid from a label
        query_response = self.daos_cmd.container_query(pool=pool.uuid, cont=cont)['response']
        cont_uuid = query_response['container_uuid']

        cont_label = query_response.get('container_label')

        # Create a TestContainer and DaosContainer instance
        container = TestContainer(pool, daos_command=self.daos_cmd)
        container.container = DaosContainer(pool.context)
        container.container.uuid = str_to_c_uuid(cont_uuid)
        container.container.poh = pool.pool.handle
        container.uuid = container.container.get_uuid_str()
        container.update_params(label=cont_label, type=query_response['container_type'])
        container.control_method.update(
            self.params.get('control_method', container.namespace, container.control_method.value))

        return container

    def parse_create_cont_label(self, output):
        """Parse a uuid or label from create container output.

        Format:
            Successfully created container (.*)

        Args:
            output (str): The string to parse for the uuid or label

        Returns:
            str: The parsed uuid or label

        """
        label_search = re.search(r"Successfully created container (.*)", output)
        if not label_search:
            self.fail("Failed to parse container label")
        return label_search.group(1).strip()

    def dataset_gen(self, cont, num_objs, num_dkeys, num_akeys_single,
                    num_akeys_array, akey_sizes, akey_extents):
        """Generate a dataset with some number of objects, dkeys, and akeys.

        Expects the container to be created with the API control method.

        Args:
            cont (TestContainer): the container.
            num_objs (int): number of objects to create in the container.
            num_dkeys (int): number of dkeys to create per object.
            num_akeys_single (int): number of DAOS_IOD_SINGLE akeys per dkey.
            num_akeys_array (int): number of DAOS_IOD_ARRAY akeys per dkey.
            akey_sizes (list): varying akey sizes to iterate.
            akey_extents (list): varying number of akey extents to iterate.

        Returns:
            list: a list of DaosObj created.

        """
        self.log.info("Creating dataset in %s/%s",
                      str(cont.pool.uuid), str(cont.uuid))

        cont.open()

        obj_list = []

        for obj_idx in range(num_objs):
            # Open the obj
            obj = DaosObj(cont.pool.context, cont.container)
            obj_list.append(obj)
            obj.create(rank=obj_idx, objcls=3)
            obj.open()

            ioreq = IORequest(cont.pool.context, cont.container, obj)
            for dkey_idx in range(num_dkeys):
                dkey = "dkey {}".format(dkey_idx)
                c_dkey = create_string_buffer(dkey)

                for akey_idx in range(num_akeys_single):
                    # Round-robin to get the size of data and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(akey_sizes)
                    data_size = akey_sizes[akey_size_idx]
                    data_val = str(akey_idx % 10)
                    data = data_size * data_val
                    akey = "akey single {}".format(akey_idx)
                    c_akey = create_string_buffer(akey)
                    c_data = create_string_buffer(data)
                    c_size = ctypes.c_size_t(ctypes.sizeof(c_data))
                    ioreq.single_insert(c_dkey, c_akey, c_data, c_size)

                for akey_idx in range(num_akeys_array):
                    # Round-robin to get the size of data and
                    # the number of extents, and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(akey_sizes)
                    data_size = akey_sizes[akey_size_idx]
                    akey_extent_idx = akey_idx % len(akey_extents)
                    num_extents = akey_extents[akey_extent_idx]
                    akey = "akey array {}".format(akey_idx)
                    c_akey = create_string_buffer(akey)
                    c_data = []
                    for data_idx in range(num_extents):
                        data_val = str(data_idx % 10)
                        data = data_size * data_val
                        c_data.append([create_string_buffer(data), data_size])
                    ioreq.insert_array(c_dkey, c_akey, c_data)

            obj.close()
        cont.close()

        return obj_list

    # pylint: disable=too-many-locals
    def dataset_verify(self, obj_list, cont, num_objs, num_dkeys,
                       num_akeys_single, num_akeys_array, akey_sizes,
                       akey_extents):
        """Verify a dataset generated with dataset_gen.

        Args:
            obj_list (list): obj_list returned from dataset_gen.
            cont (TestContainer): the container.
            num_objs (int): number of objects created in the container.
            num_dkeys (int): number of dkeys created per object.
            num_akeys_single (int): number of DAOS_IOD_SINGLE akeys per dkey.
            num_akeys_array (int): number of DAOS_IOD_ARRAY akeys per dkey.
            akey_sizes (list): varying akey sizes to iterate.
            akey_extents (list): varying number of akey extents to iterate.

        """
        self.log.info("Verifying dataset in %s/%s",
                      str(cont.pool.uuid), str(cont.uuid))

        cont.open()

        for obj_idx in range(num_objs):
            # Open the obj
            c_oid = obj_list[obj_idx].c_oid
            obj = DaosObj(cont.pool.context, cont.container, c_oid=c_oid)
            obj.open()

            ioreq = IORequest(cont.pool.context, cont.container, obj)
            for dkey_idx in range(num_dkeys):
                dkey = "dkey {}".format(dkey_idx)
                c_dkey = create_string_buffer(dkey)

                for akey_idx in range(num_akeys_single):
                    # Round-robin to get the size of data and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(akey_sizes)
                    data_size = akey_sizes[akey_size_idx]
                    data_val = str(akey_idx % 10)
                    data = data_size * data_val
                    akey = "akey single {}".format(akey_idx)
                    c_akey = create_string_buffer(akey)
                    c_data = ioreq.single_fetch(c_dkey, c_akey, data_size + 1)
                    actual_data = str(c_data.value.decode())
                    if actual_data != data:
                        self.log.info("Expected:\n%s\nBut got:\n%s",
                                      data[:100] + "...",
                                      actual_data[:100] + "...")
                        self.log.info(
                            "For:\nobj: %s.%s\ndkey: %s\nakey: %s",
                            str(obj.c_oid.hi), str(obj.c_oid.lo),
                            dkey, akey)
                        self.fail("Single value verification failed.")

                for akey_idx in range(num_akeys_array):
                    # Round-robin to get the size of data and
                    # the number of extents, and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(akey_sizes)
                    data_size = akey_sizes[akey_size_idx]
                    akey_extent_idx = akey_idx % len(akey_extents)
                    num_extents = akey_extents[akey_extent_idx]
                    akey = "akey array {}".format(akey_idx)
                    c_akey = create_string_buffer(akey)
                    c_num_extents = ctypes.c_uint(num_extents)
                    c_data_size = ctypes.c_size_t(data_size)
                    actual_data = ioreq.fetch_array(c_dkey, c_akey, c_num_extents, c_data_size)
                    for data_idx in range(num_extents):
                        data_val = str(data_idx % 10)
                        data = data_size * data_val
                        actual_idx = str(actual_data[data_idx].decode())
                        if data != actual_idx:
                            self.log.info(
                                "Expected:\n%s\nBut got:\n%s",
                                data[:100] + "...",
                                actual_idx + "...")
                            self.log.info(
                                "For:\nobj: %s.%s\ndkey: %s\nakey: %s",
                                str(obj.c_oid.hi), str(obj.c_oid.lo),
                                dkey, akey)
                            self.fail("Array verification failed.")

            obj.close()
        cont.close()

    def set_dm_params(self, src=None, dst=None, dst_pool=None):
        """Set the params for self.tool.

        Args:
            src (str, optional): src daos://pool/cont/[path] or POSIX path.
                Default is None, which is ignored.
            dst (str, optional): dst daos://pool/cont/[path] or POSIX path
                Default is None, which is ignored.
            dst_pool (TestPool/str, optional): dst pool or uuid. Only used with serialization.
                Default is None, which is ignored.
        """
        if self.tool == "DCP":
            self._set_dcp_params(src, dst)
        elif self.tool == "DSYNC":
            self._set_dsync_params(src, dst)
        elif self.tool == "DSERIAL":
            self._set_dserial_params(src, dst_pool)
        elif self.tool == "FS_COPY":
            self._set_fs_copy_params(src, dst)
        elif self.tool == "CONT_CLONE":
            self._set_cont_clone_params(src, dst)
        else:
            self.fail("Invalid tool: {}".format(self.tool))

    def set_datamover_params(self,
                             src_type=None, src_path=None, src_pool=None, src_cont=None,
                             dst_type=None, dst_path=None, dst_pool=None, dst_cont=None):
        """Set the params for self.tool.

        Called by run_datamover if params are passed.

        DEPRECATED. Use set_dm_params() instead.

        Args:
            src_type (str, optional): how to interpret the src params.
                Must be in PARAM_TYPES. Default is None.
            src_path (str, optional): source cont path or posix path. Default is None.
            src_pool (TestPool, optional): the source pool or uuid. Default is None.
            src_cont (TestContainer, optional): the source cont or uuid. Default is None.
            dst_type (str, optional): how to interpret the dst params.
                Must be in PARAM_TYPES. Default is None.
            dst_path (str, optional): destination cont path or posix path. Default is None.
            dst_pool (TestPool, optional): the destination pool or uuid. Default is None.
            dst_cont (TestContainer, optional): the destination cont or uuid. Default is None.

        """
        _src_path = None
        _dst_path = None

        if src_type == "POSIX":
            _src_path = src_path
        elif src_type in ("DAOS", "DAOS_UUID"):
            _src_path = format_path(src_pool, src_cont, src_path)
        elif src_type == "DAOS_UNS":
            _src_path = src_cont.path.value.rstrip('/') + src_path
        elif src_pool is not None and src_cont is not None:
            _src_path = format_path(src_pool, src_cont, src_path)
        elif src_cont is not None:
            _src_path = src_cont.path.value.rstrip('/') + src_path
        elif src_path is not None:
            _src_path = src_path

        if dst_type == "POSIX":
            _dst_path = dst_path
        elif dst_type in ("DAOS", "DAOS_UUID"):
            _dst_path = format_path(dst_pool, dst_cont, dst_path)
        elif dst_type == "DAOS_UNS":
            _dst_path = dst_cont.path.value.rstrip('/') + dst_path
        elif dst_pool is not None and dst_cont is not None:
            _dst_path = format_path(dst_pool, dst_cont, dst_path)
        elif dst_cont is not None:
            _dst_path = dst_cont.path.value.rstrip('/') + dst_path
        elif dst_path is not None:
            _dst_path = dst_path

        self.set_dm_params(_src_path, _dst_path, dst_pool)

    def _set_dcp_params(self, src=None, dst=None):
        """Wrapper for DcpCommand.set_params.

        Args:
            src (str): source cont path or posix path.
            dst (str): destination cont path or posix path.

        """
        # First, initialize a new dcp command
        self.dcp_cmd = DcpCommand(self.hostlist_clients, self.workdir)
        self.dcp_cmd.get_params(self)

        if self.api:
            self.dcp_cmd.set_params(daos_api=self.api)

        if src is not None:
            self.dcp_cmd.set_params(src=src)

        if dst is not None:
            self.dcp_cmd.set_params(dst=dst)

    def _set_dsync_params(self, src=None, dst=None):
        """Wrapper for DsyncCommand.set_params.

        Args:
            src (str): source cont path or posix path.
            dst (str): destination cont path or posix path.

        """
        # First, initialize a new dsync command
        self.dsync_cmd = DsyncCommand(self.hostlist_clients, self.workdir)
        self.dsync_cmd.get_params(self)

        if self.api:
            self.dsync_cmd.set_params(daos_api=self.api)

        if src is not None:
            self.dsync_cmd.set_params(src=src)

        if dst is not None:
            self.dsync_cmd.set_params(dst=dst)

    def _set_fs_copy_params(self, src=None, dst=None):
        """Set the params for fs copy.

        Args:
            src (str): source cont path or posix path.
            dst (str): destination cont path or posix path.

        """
        # First, initialize a new fs copy command
        self.fs_copy_cmd = FsCopy(self.daos_cmd, self.log)

        # set preserve-props path if it was used in test case
        if self.preserve_props_path:
            self.fs_copy_cmd.set_params(preserve_props=self.preserve_props_path)

        if src is not None:
            self.fs_copy_cmd.set_params(src=src)

        if dst is not None:
            self.fs_copy_cmd.set_params(dst=dst)

    def _set_cont_clone_params(self, src=None, dst=None):
        """Set the params for daos cont clone.

        This only supports DAOS -> DAOS copies.

        Args:
            src (str): source cont path.
            dst (str): destination cont path.

        """
        # First, initialize a new cont clone command
        self.cont_clone_cmd = ContClone(self.daos_cmd, self.log)

        # Set the source params
        if src is not None:
            self.cont_clone_cmd.set_params(src=src)

        # Set the destination params
        if dst is not None:
            self.cont_clone_cmd.set_params(dst=dst)

    def _set_dserial_params(self, src=None, dst_pool=None):
        """Set the params for daos-serialize and daos-deserialize.

        This uses a temporary POSIX path as the intermediate step
        between serializing and deserializing.

        Args:
            src (str): source cont path.
            dst_pool (TestPool, optional): the destination pool or uuid.

        """
        # First initialize new commands
        self.dserialize_cmd = DserializeCommand(self.hostlist_clients, self.workdir)
        self.ddeserialize_cmd = DdeserializeCommand(self.hostlist_clients, self.workdir)

        # Get an intermediate path for HDF5 file(s)
        tmp_path = self.new_posix_test_path(create=False, parent=self.serial_tmp_dir)

        # Set the source params for serialize
        if src is not None:
            self.dserialize_cmd.set_params(src=src, output_path=tmp_path)

        # Set the destination params for deserialize
        if dst_pool is not None:
            self.ddeserialize_cmd.set_params(src=tmp_path, pool=uuid_from_obj(dst_pool))

    def set_ior_params(self, param_type, path, pool=None, cont=None,
                       path_suffix=None, flags=None, display=True):
        """Set the ior params.

        Args:
            param_type (str): how to interpret the params.
            path (str): cont path or posix path.
            pool (TestPool, optional): the pool object
            cont (TestContainer, optional): the cont or uuid.
            path_suffix (str, optional): suffix to append to the path.
                E.g. path="/some/path", path_suffix="testFile"
            flags (str, optional): ior_cmd flags to set
            display (bool, optional): print updated params. Defaults to True.

        """
        param_type = self._validate_param_type(param_type)

        # Reset params
        self.ior_cmd.api.update(None)
        self.ior_cmd.test_file.update(None)
        self.ior_cmd.dfs_pool.update(None)
        self.ior_cmd.dfs_cont.update(None)
        self.ior_cmd.dfs_group.update(None)

        if flags:
            self.ior_cmd.flags.update(flags, "flags" if display else None)

        display_api = "api" if display else None
        display_test_file = "test_file" if display else None

        # Allow cont to be either the container or the uuid
        cont_uuid = uuid_from_obj(cont)

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
            if pool:
                self.ior_cmd.set_daos_params(self.server_group, pool, cont_uuid or None)

    def run_ior_with_params(self, param_type, path, pool=None, cont=None,
                            path_suffix=None, flags=None, display=True,
                            display_space=False):
        """Set the ior params and run ior.

        Args:
            param_type: see set_ior_params
            path: see set_ior_params
            pool: see set_ior_params
            cont: see set_ior_params
            path_suffix: see set_ior_params
            flags: see set_ior_params
            display (bool, optional): print updated params. Defaults to True.
            display_space (bool, optional): whether to display the pool space.
                Defaults to False.

        """
        self.ppn = self.ior_ppn
        self.processes = self.ior_np
        self.set_ior_params(param_type, path, pool, cont, path_suffix, flags, display)
        self.run_ior(self.get_ior_job_manager_command(), self.processes, pool=pool,
                     display_space=(display_space and bool(pool)))

    def set_mdtest_params(self, param_type, path, pool=None, cont=None, flags=None, display=True):
        """Set the mdtest params.

        Args:
            param_type (str): how to interpret the params.
            path (str): cont path or posix path.
            pool (TestPool, optional): the pool object.
            cont (TestContainer, optional): the cont or uuid.
            flags (str, optional): mdtest_cmd flags to set
            display (bool, optional): print updated params. Defaults to True.

        """
        param_type = self._validate_param_type(param_type)

        # Reset params
        self.mdtest_cmd.api.update(None)
        self.mdtest_cmd.test_dir.update(None)
        self.mdtest_cmd.dfs_pool.update(None)
        self.mdtest_cmd.dfs_cont.update(None)
        self.mdtest_cmd.dfs_group.update(None)

        if flags:
            self.mdtest_cmd.flags.update(flags, "flags" if display else None)

        display_api = "api" if display else None
        display_test_dir = "test_dir" if display else None

        # Allow cont to be either the container or the uuid
        cont_uuid = uuid_from_obj(cont)

        if param_type == "POSIX":
            self.mdtest_cmd.api.update("POSIX", display_api)
            self.mdtest_cmd.test_dir.update(path, display_test_dir)
        elif param_type in ("DAOS_UUID", "DAOS_UNS"):
            self.mdtest_cmd.api.update("DFS", display_api)
            self.mdtest_cmd.test_dir.update(path, display_test_dir)
            if pool and cont_uuid:
                self.mdtest_cmd.set_daos_params(self.server_group, pool, cont_uuid)
            elif pool:
                self.mdtest_cmd.set_daos_params(self.server_group, pool, None)

    def run_mdtest_with_params(self, param_type, path, pool=None, cont=None,
                               flags=None, display=True):
        """Set the mdtest params and run mdtest.

        Args:
            param_type: see set_ior_params
            path: see set_mdtest_params
            pool: see set_mdtest_params
            cont: see set_mdtest_params
            flags see set_mdtest_params
            display (bool, optional): print updated params. Defaults to True.

        """
        self.ppn = self.mdtest_ppn
        self.processes = self.mdtest_np
        self.set_mdtest_params(param_type, path, pool, cont, flags, display)
        self.run_mdtest(self.get_mdtest_job_manager_command(self.manager), self.processes,
                        display_space=(bool(pool)), pool=pool)

    def run_diff(self, src, dst, deref=False):
        """Run Linux diff command.

        Args:
            src (str): the source path
            dst (str): the destination path
            deref (bool, optional): Whether to dereference symlinks.
                Defaults to False.

        """
        deref_str = ""
        if not deref:
            deref_str = "--no-dereference"

        cmd = "diff -r {} '{}' '{}'".format(
            deref_str, src, dst)
        self.execute_cmd(cmd)

    # pylint: disable=too-many-arguments
    def run_datamover(self, test_desc=None,
                      src_type=None, src_path=None, src_pool=None, src_cont=None,
                      dst_type=None, dst_path=None, dst_pool=None, dst_cont=None,
                      expected_rc=0, expected_output=None, expected_err=None,
                      processes=None):
        """Run the corresponding command specified by self.tool.

        Calls set_datamover_params if and only if any are passed in.

        Args:
            test_desc (str, optional): description to print before running
            src_type: see set_datamover_params
            src_path: see set_datamover_params
            src_pool: see set_datamover_params
            src_cont: see set_datamover_params
            dst_type: see set_datamover_params
            dst_path: see set_datamover_params
            dst_cont: see set_datamover_params
            expected_rc (int, optional): rc expected to be returned
            expected_output (list, optional): substrings expected in stdout
            expected_err (list, optional): substrings expected in stderr
            processes (int, optional): number of mpi processes.
                Defaults to np for corresponding tool.

        Returns:
            The result "run" object

        """
        self.num_run_datamover += 1
        self.log.info("run_datamover called %s times",
                      str(self.num_run_datamover))

        # Set the params if and only if any were passed in
        have_src_params = (src_type or src_path or src_pool or src_cont)
        have_dst_params = (dst_type or dst_path or dst_pool or dst_cont)
        if have_src_params or have_dst_params:
            self.set_datamover_params(
                src_type, src_path, src_pool, src_cont,
                dst_type, dst_path, dst_pool, dst_cont)

        # Default expected_output and expected_err to empty lists
        if not expected_output:
            expected_output = []
        if not expected_err:
            expected_err = []

        # Convert singular value to list
        if not isinstance(expected_output, list):
            expected_output = [expected_output]
        if not isinstance(expected_err, list):
            expected_err = [expected_err]

        if test_desc is not None:
            self.log.info("Running %s: %s", self.tool, test_desc)

        env = EnvironmentVariables.from_list(
            self.params.get("env_vars", "/run/{}/*".format(self.tool.lower()), []))
        if "D_LOG_FILE" not in env:
            env["D_LOG_FILE"] = get_log_file("{}.log".format(self.tool.lower()))

        ppn = None
        result = None
        try:
            if self.tool == "DCP":
                if not processes:
                    processes = self.dcp_np
                    ppn = self.dcp_ppn
                # If we expect an rc other than 0, don't fail
                self.dcp_cmd.exit_status_exception = expected_rc == 0
                result = self.dcp_cmd.run(processes, self.job_manager, ppn, env)
            elif self.tool == "DSYNC":
                if not processes:
                    processes = self.dsync_np
                    ppn = self.dsync_ppn
                # If we expect an rc other than 0, don't fail
                self.dsync_cmd.exit_status_exception = expected_rc == 0
                result = self.dsync_cmd.run(processes, self.job_manager, ppn, env)
            elif self.tool == "DSERIAL":
                if processes:
                    processes1 = processes2 = processes
                    ppn1 = ppn2 = None
                else:
                    processes1 = self.dserialize_np
                    ppn1 = self.dserialize_ppn
                    processes2 = self.ddeserialize_np
                    ppn2 = self.ddeserialize_ppn
                result = self.dserialize_cmd.run(processes1, self.job_manager, ppn1, env)
                result = self.ddeserialize_cmd.run(processes2, self.job_manager, ppn2, env)
            elif self.tool == "FS_COPY":
                result = self.fs_copy_cmd.run()
            elif self.tool == "CONT_CLONE":
                result = self.cont_clone_cmd.run()
            else:
                self.fail("Invalid tool: {}".format(str(self.tool)))
        except CommandFailure as error:
            self.log.error("%s command failed: %s", str(self.tool), str(error))
            self.fail("Test was expected to pass but it failed: {}\n".format(
                test_desc))

        # Check the return code
        assert result is not None
        actual_rc = result.exit_status
        if actual_rc != expected_rc:
            self.fail("Expected (rc={}) but got (rc={}): {}\n".format(
                expected_rc, actual_rc, test_desc))

        # Check for expected output
        for expected in expected_output:
            if expected not in result.stdout_text:
                self.fail("stdout expected {}: {}".format(expected, test_desc))
        for expected in expected_err:
            if expected not in result.stderr_text:
                self.fail("stderr expected {}: {}".format(expected, test_desc))

        return result

    def run_dm_activities_with_ior(self, tool, pool, cont, create_dataset=False):
        """Generic method to perform various datamover activities using ior

        Args:
            tool (str): specify the tool name to be used
            pool (TestPool): source pool object
            cont (TestContainer): source container object
            create_dataset (bool): boolean to create initial set of
                                   data using ior. Defaults to False.
        """
        # Set the tool to use
        self.set_tool(tool)

        if create_dataset:
            # create initial data-sets with ior
            self.run_ior_with_params("DAOS", self.ior_cmd.test_file.value, pool, cont)

        # create cont2
        cont2 = self.get_container(pool, oclass=self.ior_cmd.dfs_oclass.value)

        # perform various datamover activities
        if tool == 'CONT_CLONE':
            result = self.run_datamover(
                self.test_id + " (cont to cont2)",
                src_path=format_path(pool, cont),
                dst_path=format_path(pool))
            read_back_cont = self.parse_create_cont_label(result.stdout_text)
            read_back_pool = pool
        elif tool == 'DSERIAL':
            # Create pool2
            pool2 = self.get_pool()
            # Use dfuse as a shared intermediate for serialize + deserialize
            dfuse_cont = self.get_container(pool, oclass=self.ior_cmd.dfs_oclass.value)
            self.start_dfuse(self.dfuse_hosts, pool, dfuse_cont)
            self.serial_tmp_dir = self.dfuse.mount_dir.value

            # Serialize/Deserialize container 1 to a new cont2 in pool2
            result = self.run_datamover(
                self.test_id + " (cont->HDF5->cont2)",
                src_path=format_path(pool, cont),
                dst_pool=pool2)

            # Get the destination cont2 uuid
            read_back_cont = self.parse_create_cont_label(result.stdout_text)
            read_back_pool = pool2
        elif tool in ['FS_COPY', 'DCP']:
            # copy from daos cont to cont2
            self.run_datamover(
                self.test_id + " (cont to cont2)",
                src_path=format_path(pool, cont),
                dst_path=format_path(pool, cont2))
        else:
            self.fail("Invalid tool: {}".format(tool))

        # move data from daos to posix FS and vice versa
        if tool in ['FS_COPY', 'DCP']:
            posix_path = self.new_posix_test_path(shared=True)
            # copy from daos cont2 to posix file system
            self.run_datamover(
                self.test_id + " (cont2 to posix)",
                src_path=format_path(pool, cont2),
                dst_path=posix_path)

            # create cont3
            cont3 = self.get_container(pool, oclass=self.ior_cmd.dfs_oclass.value)

            # copy from posix file system to daos cont3
            self.run_datamover(
                self.test_id + " (posix to cont3)",
                src_path=posix_path,
                dst_path=format_path(pool, cont3))
            read_back_cont = cont3
            read_back_pool = pool
        test_file = os.path.basename(self.ior_cmd.test_file.value)
        if tool in ['FS_COPY', 'DCP']:
            # the result is that a NEW directory is created in the destination
            daos_path = os.path.join(os.sep, os.path.basename(posix_path), test_file)
        elif tool in ['CONT_CLONE', 'DSERIAL']:
            daos_path = os.path.join(os.sep, test_file)
        else:
            self.fail("Invalid tool: {}".format(tool))
        # update ior params, read back and verify data from cont3
        self.run_ior_with_params(
            "DAOS", daos_path, read_back_pool, read_back_cont,
            flags="-r -R -F -k")

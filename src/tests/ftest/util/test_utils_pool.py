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
import ctypes
import os
from grp import getgrgid
from pwd import getpwuid
from time import sleep

from avocado import fail_on
from avocado.utils import process

from command_utils import BasicParameter, CommandFailure
from dmg_utils import get_pool_uuid_service_replicas_from_stdout
from env_modules import load_mpi
from general_utils import check_pool_files, DaosTestError
from pydaos.raw import (DaosApiError, DaosServer, DaosPool, c_uuid_to_str,
                        daos_cref)
from test_utils_base import TestDaosApiBase


class TestPool(TestDaosApiBase):
    # pylint: disable=too-many-public-methods
    """A class for functional testing of DaosPools objects."""

    # Constants to define whether to use API or dmg to create and destroy
    # pool.
    USE_API = "API"
    USE_DMG = "dmg"

    def __init__(self, context, cb_handler=None, dmg_command=None):
        """Initialize a TestPool object.

        Note: 'log' is now a defunct argument and will be removed in the future

        Args:
            context (DaosContext): [description]
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
            dmg_command (DmgCommand): DmgCommand object, already configured
                to comunicate with the daos servers, used to create and destroy
                pools if so configured (see control_method). Defaults to None.
        """
        super(TestPool, self).__init__("/run/pool/*", cb_handler)
        self.context = context
        self.uid = os.geteuid()
        self.gid = os.getegid()

        self.mode = BasicParameter(None)
        self.name = BasicParameter(None)            # server group name
        self.svcn = BasicParameter(1, 1)
        self.target_list = BasicParameter(None)
        self.scm_size = BasicParameter(None)
        self.nvme_size = BasicParameter(0, 0)

        # Set USE_API to use API or USE_DMG to use dmg. If it's not set, API is
        # used.
        self.control_method = BasicParameter(self.USE_API, self.USE_API)

        self.pool = None
        self.uuid = None
        self.info = None
        self.svc_ranks = None
        self.connected = False
        self.dmg = dmg_command

    @property
    def using_dmg(self):
        """Determine if dmg is being used as the control method.

        Returns:
            bool: True if dmg is being used as the control method

        """
        return self.control_method.value == self.USE_DMG

    @fail_on(CommandFailure)
    @fail_on(DaosApiError)
    def create(self):
        """Create a pool with either API or dmg.

        To use dmg, the test needs to set control_method.value to USE_DMG
        prior to calling this method. The recommended way is to specify the
        pool block in yaml. For example:

            pool:
                control_method: dmg

        This tells this method to use dmg. The test also needs to provide a
        DaosCommand object for the dmg argument of the class constructor in
        order to use the dmg control method. For example:

            self.pool = TestPool(
                self.context, dmg_command=self.get_dmg_command())

        The current uid, gid, name, svcn, target_list, scm_size, and nvme_size
        values are mapped to the appropriate dmg pool create command line
        arguments.

        In either case, using dmg or the API, if an error is encountered,
        possibly for negative testing, this method will raise a TestFail
        exception.
        """
        self.destroy()
        if self.target_list.value is not None:
            self.log.info(
                "Creating a pool on targets %s", self.target_list.value)
        else:
            self.log.info("Creating a pool")
        self.pool = DaosPool(self.context)
        if self.control_method.value == self.USE_API:
            kwargs = {
                "mode": self.mode.value,
                "uid": self.uid,
                "gid": self.gid,
                "scm_size": self.scm_size.value,
                "group": self.name.value,
                "target_list": self.target_list.value,
                "svcn": self.svcn.value,
                "nvme_size": self.nvme_size.value,
            }
            self._call_method(self.pool.create, kwargs)
        else:
            if self.dmg is None:
                raise DaosTestError(
                    "self.dmg is None. dmg_command needs to be set through "
                    "the constructor of TestPool to create pool with dmg.")
            # Call the dmg pool create command
            self.dmg.request.value = "pool"
            self.dmg.action.value = "create"
            self.dmg.get_action_command()

            # Define the "dmg pool create" command parameter values
            if self.uid is not None:
                # Convert the user ID to a user name for the dmg command
                self.dmg.action_command.user.value = getpwuid(self.uid).pw_name
            else:
                self.dmg.action_command.user.value = None
            if self.gid is not None:
                # Convert the group ID to a group name for the dmg command
                self.dmg.action_command.group.value = getgrgid(self.gid).gr_name
            else:
                self.dmg.action_command.group.value = None
            self.dmg.action_command.scm_size.value = self.scm_size.value
            if isinstance(self.target_list.value, list):
                # Convert the list of target ranks into a comma-separated list
                self.dmg.action_command.ranks.value = ",".join(
                    [str(target) for target in self.target_list.value])
            else:
                self.dmg.action_command.ranks.value = self.target_list.value
            self.dmg.action_command.nsvc.value = self.svcn.value

            # Execute the dmg command to get the pool uuid and svc list
            create_result = self.dmg.run()
            self.log.info("Result stdout = %s", create_result.stdout)
            self.log.info("Result exit status = %s", create_result.exit_status)

            # Get UUID and service replica from the output
            uuid, svc = get_pool_uuid_service_replicas_from_stdout(
                create_result.stdout)

            # 3. Create DaosPool object. The process is similar to the one in
            # DaosPool.create, but there are some modifications

            # Populte the empty DaosPool object with the properties of the pool
            # created with dmg pool create.
            if self.name.value is not None:
                self.pool.group = ctypes.create_string_buffer(self.name.value)

            # Convert the string of service replicas from the dmg command output
            # into an ctype array for the DaosPool object using the same
            # technique used in DaosPool.create().
            service_replicas = [int(value) for value in svc.split(",")]
            rank_t = ctypes.c_uint * len(service_replicas)
            rank = rank_t(*list([svc for svc in service_replicas]))
            rl_ranks = ctypes.POINTER(ctypes.c_uint)(rank)
            self.pool.svc = daos_cref.RankList(rl_ranks, len(service_replicas))

            # Set UUID and attached for the DaosPool object
            self.pool.set_uuid_str(uuid)
            self.pool.attached = 1

        # Set the TestPool attributes for the created pool
        self.svc_ranks = [
            int(self.pool.svc.rl_ranks[index])
            for index in range(self.pool.svc.rl_nr)]
        self.uuid = self.pool.get_uuid_str()

    @fail_on(DaosApiError)
    def connect(self, permission=2):
        """Connect to the pool.

        Args:
            permission (int, optional): connect permission. Defaults to 2.

        Returns:
            bool: True if the pool has been connected; False if the pool was
                already connected or the pool is not defined.

        """
        if self.pool and not self.connected:
            kwargs = {"flags": permission}
            self.log.info(
                "Connecting to pool %s with permission %s (flag: %s)",
                self.uuid, permission, kwargs["flags"])
            self._call_method(self.pool.connect, kwargs)
            self.connected = True
            return True
        return False

    @fail_on(DaosApiError)
    def disconnect(self):
        """Disconnect from connected pool.

        Returns:
            bool: True if the pool has been disconnected; False if the pool was
                already disconnected or the pool is not defined.

        """
        if self.pool and self.connected:
            self.log.info("Disonnecting from pool %s", self.uuid)
            self._call_method(self.pool.disconnect, {})
            self.connected = False
            return True
        return False

    @fail_on(CommandFailure)
    @fail_on(DaosApiError)
    def destroy(self, force=1):
        """Destroy the pool with either API or dmg.

        It uses control_method member previously set, so if you want to use the
        other way for some reason, update it before calling this method.

        Args:
            force (int, optional): force flag. Defaults to 1.

        Returns:
            bool: True if the pool has been destroyed; False if the pool is not
                defined.

        """
        status = False
        if self.pool:
            self.disconnect()
            if self.pool.attached:
                self.log.info("Destroying pool %s", self.uuid)

                if self.control_method.value == self.USE_API:
                    self._call_method(self.pool.destroy, {"force": force})
                    status = True

                elif self.dmg is not None:
                    self.dmg.action.value = "destroy"
                    self.dmg.get_action_command()
                    self.dmg.action_command.pool.value = self.uuid
                    self.dmg.action_command.force.value = force
                    self.dmg.run()
                    status = True

                elif self.control_method.value == self.USE_DMG:
                    self.log.error("Error: Undefined dmg command")

                else:
                    self.log.error(
                        "Error: Undefined control_method: %s",
                        self.control_method.value)

            self.pool = None
            self.uuid = None
            self.info = None
            self.svc_ranks = None

        return status

    @fail_on(DaosApiError)
    def get_info(self):
        """Query the pool for information.

        Sets the self.info attribute.
        """
        if self.pool:
            self.connect()
            self._call_method(self.pool.pool_query, {})
            self.info = self.pool.pool_info

    def check_pool_info(self, pi_uuid=None, pi_ntargets=None, pi_nnodes=None,
                        pi_ndisabled=None, pi_map_ver=None, pi_leader=None,
                        pi_bits=None):
        # pylint: disable=unused-argument
        """Check the pool info attributes.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Args:
            pi_uuid (str, optional): pool uuid. Defaults to None.
            pi_ntargets (int, optional): number of targets. Defaults to None.
            pi_nnodes (int, optional): number of nodes. Defaults to None.
            pi_ndisabled (int, optional): number of disabled. Defaults to None.
            pi_map_ver (int, optional): pool map version. Defaults to None.
            pi_leader (int, optional): pool leader. Defaults to None.
            pi_bits (int, optional): pool bits. Defaults to None.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Returns:
            bool: True if at least one expected value is specified and all the
                specified values match; False otherwise

        """
        self.get_info()
        checks = [
            (key,
             c_uuid_to_str(getattr(self.info, key))
             if key == "pi_uuid" else getattr(self.info, key),
             val)
            for key, val in locals().items()
            if key != "self" and val is not None]
        return self._check_info(checks)

    def check_pool_space(self, ps_free_min=None, ps_free_max=None,
                         ps_free_mean=None, ps_ntargets=None, ps_padding=None):
        # pylint: disable=unused-argument
        """Check the pool info space attributes.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Args:
            ps_free_min (list, optional): minimum free space per device.
                Defaults to None.
            ps_free_max (list, optional): maximum free space per device.
                Defaults to None.
            ps_free_mean (list, optional): mean free space per device.
                Defaults to None.
            ps_ntargets (int, optional): number of targets. Defaults to None.
            ps_padding (int, optional): space padding. Defaults to None.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Returns:
            bool: True if at least one expected value is specified and all the
                specified values match; False otherwise

        """
        self.get_info()
        checks = []
        for key in ("ps_free_min", "ps_free_max", "ps_free_mean"):
            val = locals()[key]
            if isinstance(val, list):
                for index, item in val:
                    checks.append((
                        "{}[{}]".format(key, index),
                        getattr(self.info.pi_space, key)[index],
                        item))
        for key in ("ps_ntargets", "ps_padding"):
            val = locals()[key]
            if val is not None:
                checks.append(key, getattr(self.info.pi_space, key), val)
        return self._check_info(checks)

    def check_pool_daos_space(self, s_total=None, s_free=None):
        # pylint: disable=unused-argument
        """Check the pool info daos space attributes.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Args:
            s_total (list, optional): total space per device. Defaults to None.
            s_free (list, optional): free space per device. Defaults to None.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Returns:
            bool: True if at least one expected value is specified and all the
                specified values match; False otherwise

        """
        self.get_info()
        checks = [
            ("{}_{}".format(key, index),
             getattr(self.info.pi_space.ps_space, key)[index],
             item)
            for key, val in locals().items()
            if key != "self" and val is not None
            for index, item in enumerate(val)]
        return self._check_info(checks)

    def check_rebuild_status(self, rs_version=None, rs_seconds=None,
                             rs_errno=None, rs_done=None, rs_padding32=None,
                             rs_fail_rank=None, rs_toberb_obj_nr=None,
                             rs_obj_nr=None, rs_rec_nr=None, rs_size=None):
        # pylint: disable=unused-argument
        # pylint: disable=too-many-arguments
        """Check the pool info rebuild attributes.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Args:
            rs_version (int, optional): rebuild version. Defaults to None.
            rs_seconds (int, optional): rebuild seconds. Defaults to None.
            rs_errno (int, optional): rebuild error number. Defaults to None.
            rs_done (int, optional): rebuild done flag. Defaults to None.
            rs_padding32 (int, optional): padding. Defaults to None.
            rs_fail_rank (int, optional): rebuild fail target. Defaults to None.
            rs_toberb_obj_nr (int, optional): number of objects to be rebuilt.
                Defaults to None.
            rs_obj_nr (int, optional): number of rebuilt objects.
                Defaults to None.
            rs_rec_nr (int, optional): number of rebuilt records.
                Defaults to None.
            rs_size (int, optional): size of all rebuilt records.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Returns:
            bool: True if at least one expected value is specified and all the
                specified values match; False otherwise

        """
        self.get_info()
        checks = [
            (key, getattr(self.info.pi_rebuild_st, key), val)
            for key, val in locals().items()
            if key != "self" and val is not None]
        return self._check_info(checks)

    def rebuild_complete(self):
        """Determine if the pool rebuild is complete.

        Returns:
            bool: True if pool rebuild is complete; False otherwise

        """
        self.display_pool_rebuild_status()
        return self.info.pi_rebuild_st.rs_done == 1

    def wait_for_rebuild(self, to_start, interval=1):
        """Wait for the rebuild to start or end.

        Args:
            to_start (bool): whether to wait for rebuild to start or end
            interval (int): number of seconds to wait in between rebuild
                completion checks
        """
        self.log.info(
            "Waiting for rebuild to %s ...",
            "start" if to_start else "complete")
        while self.rebuild_complete() == to_start:
            self.log.info(
                "  Rebuild %s ...",
                "has not yet started" if to_start else "in progress")
            sleep(interval)
        self.log.info(
            "Rebuild %s detected", "start" if to_start else "completion")

    @fail_on(DaosApiError)
    def start_rebuild(self, ranks, daos_log):
        """Kill the specific server ranks using this pool.

        Args:
            ranks (list): a list of daos server ranks (int) to kill
            daos_log (DaosLog): object for logging messages

        Returns:
            bool: True if the server ranks have been killed and the ranks have
            been excluded from the pool; False if the pool is undefined

        """
        msg = "Killing DAOS ranks {} from server group {}".format(
            ranks, self.name.value)
        self.log.info(msg)
        daos_log.info(msg)
        for rank in ranks:
            server = DaosServer(self.context, self.name.value, rank)
            self._call_method(server.kill, {"force": 1})
        return self.exclude(ranks, daos_log)

    @fail_on(DaosApiError)
    def exclude(self, ranks, daos_log):
        """Manually exclude a rank from this pool.

        Args:
            ranks (list): a list daos server ranks (int) to exclude
            daos_log (DaosLog): object for logging messages

        Returns:
            bool: True if the ranks were excluded from the pool; False if the
                pool is undefined

        """
        if self.pool:
            msg = "Excluding server ranks {} from pool {}".format(
                ranks, self.uuid)
            self.log.info(msg)
            daos_log.info(msg)
            self._call_method(self.pool.exclude, {"rank_list": ranks})
            return True
        return False

    def check_files(self, hosts):
        """Check if pool files exist on the specified list of hosts.

        Args:
            hosts (list): list of hosts

        Returns:
            bool: True if the files for this pool exist on each host; False
                otherwise

        """
        return check_pool_files(self.log, hosts, self.uuid.lower())

    def write_file(self, orterun, processes, hostfile, size, timeout=60):
        """Write a file to the pool.

        Args:
            orterun (str): full path to the orterun command
            processes (int): number of processes to launch
            hosts (list): list of clients from which to write the file
            size (int): size of the file to create in bytes
            timeout (int, optional): number of seconds before timing out the
                command. Defaults to 60 seconds.

        Returns:
            process.CmdResult: command execution result

        """
        self.log.info("Writing %s bytes to pool %s", size, self.uuid)
        env = {
            "DAOS_POOL": self.uuid,
            "DAOS_SVCL": "1",
            "DAOS_SINGLETON_CLI": "1",
            "PYTHONPATH": os.getenv("PYTHONPATH", ""),
        }
        load_mpi("openmpi")
        current_path = os.path.dirname(os.path.abspath(__file__))
        command = "{} --np {} --hostfile {} {} {} testfile".format(
            orterun, processes, hostfile,
            os.path.join(current_path, "write_some_data.py"), size)
        return process.run(command, timeout, True, False, "both", True, env)

    def get_pool_daos_space(self):
        """Get the pool info daos space attributes as a dictionary.

        Returns:
            dict: a dictionary of lists of the daos space attributes

        """
        self.get_info()
        keys = ("s_total", "s_free")
        return {key: getattr(self.info.pi_space.ps_space, key) for key in keys}

    def display_pool_daos_space(self, msg=None):
        """Display the pool info daos space attributes.

        Args:
            msg (str, optional): optional text to include in the output.
                Defaults to None.
        """
        daos_space = self.get_pool_daos_space()
        sizes = [
            "{}[{}]={}".format(key, index, item)
            for key in sorted(daos_space.keys())
            for index, item in enumerate(daos_space[key])]
        self.log.info(
            "Pool %s space%s:\n  %s", self.uuid,
            " " + msg if isinstance(msg, str) else "", "\n  ".join(sizes))

    def get_pool_rebuild_status(self):
        """Get the pool info rebuild status attributes as a dictionary.

        Returns:
            dict: a dictionary of lists of the rebuild status attributes

        """
        self.get_info()
        keys = (
            "rs_version", "rs_pad_32", "rs_errno", "rs_done",
            "rs_toberb_obj_nr", "rs_obj_nr", "rs_rec_nr")
        return {key: getattr(self.info.pi_rebuild_st, key) for key in keys}

    def display_pool_rebuild_status(self):
        """Display the pool info rebuild status attributes."""
        status = self.get_pool_rebuild_status()
        self.log.info(
            "Pool rebuild status: %s",
            ", ".join(
                ["{}={}".format(key, status[key]) for key in sorted(status)]))

    def read_data_during_rebuild(self, container):
        """Read data from the container while rebuild is active.

        Args:
            container (TestContainer): container from which to read data

        Returns:
            bool: True if all the data is read sucessfully befoire rebuild
                completes; False otherwise

        """
        container.open()
        self.log.info(
            "Reading objects in container %s during rebuild", self.uuid)

        # Attempt to read all of the data from the container during rebuild
        index = 0
        status = read_incomplete = index < len(container.written_data)
        while not self.rebuild_complete() and read_incomplete:
            try:
                status &= container.written_data[index].read_object(container)
            except DaosTestError as error:
                self.log.error(str(error))
                status = False
            index += 1
            read_incomplete = index < len(container.written_data)

        # Verify that all of the container data was read successfully
        if read_incomplete:
            self.log.error(
                "Rebuild completed before all the written data could be read")
            status = False
        elif not status:
            self.log.error("Errors detected reading data during rebuild")
        return status

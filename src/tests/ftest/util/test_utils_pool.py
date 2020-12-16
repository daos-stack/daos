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
import os
from time import sleep
import ctypes

from test_utils_base import TestDaosApiBase

from avocado import fail_on
from command_utils import BasicParameter, CommandFailure
from pydaos.raw import (DaosApiError, DaosServer, DaosPool, c_uuid_to_str,
                        daos_cref)
from general_utils import (check_pool_files, DaosTestError, run_command,
                           convert_list)
from env_modules import load_mpi


class TestPool(TestDaosApiBase):
    # pylint: disable=too-many-public-methods
    """A class for functional testing of DaosPools objects."""

    def __init__(self, context, dmg_command, cb_handler=None):
        # pylint: disable=unused-argument
        """Initialize a TestPool object.

        Args:
            context (DaosContext): [description]
            dmg_command (DmgCommand): DmgCommand used to call dmg command. This
                value can be obtained by calling self.get_dmg_command() from a
                test. It'll return the object with -l <Access Point host:port>
                and --insecure.
            log (logging): logging object used to report the pool status
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
        """
        super(TestPool, self).__init__("/run/pool/*", cb_handler)
        self.context = context
        self.uid = os.geteuid()
        self.gid = os.getegid()

        self.mode = BasicParameter(None)
        self.name = BasicParameter(None)            # server group name
        self.svcn = BasicParameter(None)
        self.target_list = BasicParameter(None)
        self.scm_size = BasicParameter(None)
        self.nvme_size = BasicParameter(None)
        self.prop_name = BasicParameter(None)       # name of property to be set
        self.prop_value = BasicParameter(None)      # value of property

        self.pool = None
        self.uuid = None
        self.info = None
        self.svc_ranks = None
        self.connected = False

        self.dmg = dmg_command
        self.query_data = []

    @fail_on(CommandFailure)
    @fail_on(DaosApiError)
    def create(self):
        """Create a pool with dmg.

        To use dmg, the test needs to set dmg_command through the constructor.
        For example,

            self.pool = TestPool(self.context, DmgCommand(self.bin))

        If it wants to use --nsvc option, it needs to set the value to
        svcn.value. Otherwise, 1 is used. If it wants to use --group, it needs
        to set groupname.value. If it wants to use --user, it needs to set
        username.value. If it wants to add other options, directly set it
        to self.dmg.action_command. Refer dmg_utils.py pool_create method for
        more details.

        To test the negative case on create, the test needs to catch
        CommandFailure. Thus, we need to make more than one line modification
        to the test only for this purpose.
        Currently, pool_svc is the only test that needs this change.
        """
        self.destroy()
        if self.target_list.value is not None:
            self.log.info(
                "Creating a pool on targets %s", self.target_list.value)
        else:
            self.log.info("Creating a pool")

        self.pool = DaosPool(self.context)
        kwargs = {
            "uid": self.uid,
            "gid": self.gid,
            "scm_size": self.scm_size.value,
            "group": self.name.value}
        for key in ("target_list", "svcn", "nvme_size"):
            value = getattr(self, key).value
            if value is not None:
                kwargs[key] = value

        if self.control_method.value == self.USE_API:
            raise CommandFailure(
                "Error: control method {} not supported for create()".format(
                    self.control_method.value))

        elif self.control_method.value == self.USE_DMG and self.dmg:
            # Create a pool with the dmg command and store its CmdResult
            self._log_method("dmg.pool_create", kwargs)
            data = self.dmg.pool_create(**kwargs)
            if self.dmg.result.exit_status == 0:
                # Populate the empty DaosPool object with the properties of the
                # pool created with dmg pool create.
                if self.name.value:
                    self.pool.group = ctypes.create_string_buffer(
                        self.name.value)

                # Convert the string of service replicas from the dmg command
                # output into an ctype array for the DaosPool object using the
                # same technique used in DaosPool.create().
                service_replicas = [
                    int(value) for value in data["svc"].split(",")]
                rank_t = ctypes.c_uint * len(service_replicas)
                rank = rank_t(*list([svc for svc in service_replicas]))
                rl_ranks = ctypes.POINTER(ctypes.c_uint)(rank)
                self.pool.svc = daos_cref.RankList(
                    rl_ranks, len(service_replicas))

                # Set UUID and attached to the DaosPool object
                self.pool.set_uuid_str(data["uuid"])
                self.pool.attached = 1

        elif self.control_method.value == self.USE_DMG:
            self.log.error("Error: Undefined dmg command")

        else:
            self.log.error(
                "Error: Undefined control_method: %s",
                self.control_method.value)

        # Set the TestPool attributes for the created pool
        if self.pool.attached:
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
            self.log.info("Disconnecting from pool %s", self.uuid)
            self._call_method(self.pool.disconnect, {})
            self.connected = False
            return True
        return False

    @fail_on(CommandFailure)
    @fail_on(DaosApiError)
    def destroy(self, force=1, disconnect=1):
        """Destroy the pool with either API or dmg.

        It uses control_method member previously set, so if you want to use the
        other way for some reason, update it before calling this method.

        Args:
            force (int, optional): force flag. Defaults to 1.
            disconnect (int, optional): disconnect flag. Defaults to 1.

        Returns:
            bool: True if the pool has been destroyed; False if the pool is not
                defined.

        """
        status = False
        if self.pool:
            if disconnect:
                self.disconnect()
            if self.pool.attached:
                self.log.info("Destroying pool %s", self.uuid)

                if self.control_method.value == self.USE_API:
                    # Destroy the pool with the API method
                    self._call_method(self.pool.destroy, {"force": force})
                    status = True

                elif self.control_method.value == self.USE_DMG and self.dmg:
                    # Destroy the pool with the dmg command
                    self.dmg.pool_destroy(pool=self.uuid, force=force)
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

    @fail_on(CommandFailure)
    def set_property(self, prop_name=None, prop_value=None):
        """Set Property.

        It sets property for a given pool uuid using
        dmg.

        Args:
            prop_name (str, optional): pool property name. Defaults to
                None, which uses the TestPool.prop_name.value
            prop_value (str, optional): value to be set for the property.
                Defaults to None, which uses the TestPool.prop_value.value

        Returns:
            None

        """
        if self.pool:
            self.log.info("Set-prop for Pool: %s", self.uuid)

            if self.control_method.value == self.USE_DMG and self.dmg:
                # If specific values are not provided, use the class values
                if prop_name is None:
                    prop_name = self.prop_name.value
                if prop_value is None:
                    prop_value = self.prop_value.value
                self.dmg.pool_set_prop(self.uuid, prop_name, prop_value)

            elif self.control_method.value == self.USE_DMG:
                self.log.error("Error: Undefined dmg command")

            else:
                self.log.error(
                    "Error: Undefined control_method: %s",
                    self.control_method.value)

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
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
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
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
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
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
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
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
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
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
            default '=='.

        Args:
            s_total (list, optional): total space per device. Defaults to None.
            s_free (list, optional): free space per device. Defaults to None.

        Note:
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
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
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
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
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
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
    @fail_on(CommandFailure)
    def start_rebuild(self, ranks, daos_log):
        """Kill/Stop the specific server ranks using this pool.

        Args:
            ranks (list): a list of daos server ranks (int) to kill
            daos_log (DaosLog): object for logging messages

        Returns:
            bool: True if the server ranks have been killed/stopped and the
            ranks have been excluded from the pool; False otherwise.

        """
        msg = "Killing DAOS ranks {} from server group {}".format(
            ranks, self.name.value)
        self.log.info(msg)
        daos_log.info(msg)

        if self.control_method.value == self.USE_API:
            # Stop desired ranks using kill
            for rank in ranks:
                server = DaosServer(self.context, self.name.value, rank)
                self._call_method(server.kill, {"force": 1})
            return True

        elif self.control_method.value == self.USE_DMG and self.dmg:
            # Stop desired ranks using dmg
            self.dmg.system_stop(ranks=convert_list(value=ranks))
            return True

        elif self.control_method.value == self.USE_DMG:
            self.log.error("Error: Undefined dmg command")

        else:
            self.log.error(
                "Error: Undefined control_method: %s",
                self.control_method.value)

        return False

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
        #env = {
        #    "DAOS_POOL": self.uuid,
        #    "DAOS_SVCL": "1",
        #    "PYTHONPATH": os.getenv("PYTHONPATH", "")
        #}
        env = {
            "DAOS_POOL": self.uuid,
            "PYTHONPATH": os.getenv("PYTHONPATH", "")
        }
        if not load_mpi("openmpi"):
            raise CommandFailure("Failed to load openmpi")

        current_path = os.path.dirname(os.path.abspath(__file__))
        command = "{} --np {} --hostfile {} {} {} testfile".format(
            orterun, processes, hostfile,
            os.path.join(current_path, "write_some_data.py"), size)
        return run_command(command, timeout, True, env=env)

    def get_pool_daos_space(self):
        """Get the pool info daos space attributes as a dictionary.

        Returns:
            dict: a dictionary of lists of the daos space attributes

        """
        self.get_info()
        keys = ("s_total", "s_free")
        return {key: getattr(self.info.pi_space.ps_space, key) for key in keys}

    def get_pool_free_space(self, device="scm"):
        """Get SCM or NVME free space.

        Args:
            device (str, optional): device type, e.g. "scm" or "nvme". Defaults
                to "scm".

        Returns:
            str: free SCM or NVME space

        """
        free_space = "0"
        dev = device.lower()
        daos_space = self.get_pool_daos_space()
        if dev == "scm":
            free_space = daos_space["s_free"][0]
        elif dev == "nvme":
            free_space = daos_space["s_free"][1]
        return free_space

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

    def pool_percentage_used(self):
        """Get the pool storage used % for SCM and NVMe.

        Returns:
            dict: a dictionary of SCM/NVMe pool space usage in %(float)

        """
        daos_space = self.get_pool_daos_space()
        pool_percent = {'scm': round(float(daos_space["s_free"][0]) /
                                     float(daos_space["s_total"][0]) * 100, 2),
                        'nvme': round(float(daos_space["s_free"][1]) /
                                      float(daos_space["s_total"][1]) * 100, 2)}
        return pool_percent

    def get_pool_rebuild_status(self):
        """Get the pool info rebuild status attributes as a dictionary.

        Returns:
            dict: a dictionary of lists of the rebuild status attributes

        """
        self.get_info()
        keys = (
            "rs_version", "rs_padding32", "rs_errno", "rs_done",
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
            bool: True if all the data is read successfully before rebuild
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

    @fail_on(CommandFailure)
    def set_query_data(self):
        """Execute dmg pool query and store the results.

        Only supported with the dmg control method.
        """
        self.query_data = []
        if self.pool:
            if self.dmg:
                self.query_data = self.dmg.pool_query(self.pool.get_uuid_str())
            else:
                self.log.error("Error: Undefined dmg command")

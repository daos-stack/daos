#!/usr/bin/python
"""
  (C) Copyright 2018-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from time import sleep, time
import ctypes

from test_utils_base import TestDaosApiBase

from avocado import fail_on
from command_utils import BasicParameter, CommandFailure
from pydaos.raw import (DaosApiError, DaosPool, c_uuid_to_str, daos_cref)
from general_utils import check_pool_files, DaosTestError, run_command
from env_modules import load_mpi
from server_utils_base import ServerFailed, AutosizeCancel


class TestPool(TestDaosApiBase):
    # pylint: disable=too-many-public-methods
    """A class for functional testing of DaosPools objects."""

    def __init__(self, context, dmg_command, cb_handler=None,
                 label_generator=None):
        # pylint: disable=unused-argument
        """Initialize a TestPool object.

        Args:
            context (DaosContext): The daos environment and other info. Use
                self.context when calling from a test.
            dmg_command (DmgCommand): DmgCommand used to call dmg command. This
                value can be obtained by calling self.get_dmg_command() from a
                test. It'll return the object with -l <Access Point host:port>
                and --insecure.
            log (logging): logging object used to report the pool status
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
            label_generator (LabelGenerator, optional): Generates label by
                adding number to the end of the prefix set in self.label.
                There's a link between label_generator and label. If the label
                is used as it is, i.e., not None, label_generator must be
                provided in order to call create(). Defaults to None.
        """
        super().__init__("/run/pool/*", cb_handler)
        self.context = context
        self.uid = os.geteuid()
        self.gid = os.getegid()

        self.mode = BasicParameter(None)
        self.name = BasicParameter(None)            # server group name
        self.svcn = BasicParameter(None)
        self.target_list = BasicParameter(None)
        self.size = BasicParameter(None)
        self.tier_ratio = BasicParameter(None)
        self.scm_size = BasicParameter(None)
        self.nvme_size = BasicParameter(None)
        self.prop_name = BasicParameter(None)       # name of property to be set
        self.prop_value = BasicParameter(None)      # value of property
        self.properties = BasicParameter(None)      # string of cs name:value
        self.rebuild_timeout = BasicParameter(None)
        self.pool_query_timeout = BasicParameter(None)
        self.label = BasicParameter(None, "TestLabel")
        self.label_generator = label_generator

        # Optional TestPool parameters used to autosize the dmg pool create
        # 'size', 'scm_size', and/or 'nvme_size' values:
        #   server_index: TestWithServers.server_managers list index
        #   quantity:     number of pools to account for in sizing
        #   min_targets:  minimum number of targets allowed
        self.server_index = BasicParameter(None, 0)
        self.quantity = BasicParameter(None, 1)
        self.min_targets = BasicParameter(None, 1)

        self.pool = None
        self.uuid = None
        self.info = None
        self.svc_ranks = None
        self.connected = False
        # Flag to allow the non-create operations to use UUID. e.g., if you want
        # to destroy the pool with UUID, set this to False, then call destroy().
        self.use_label = True

        self.dmg = dmg_command
        self.query_data = []

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Autosize any size/scm_size/nvme_size parameter whose value ends in "%".
        Also create a unique label by adding the incremented number prefix.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)

        # Autosize any size/scm_size/nvme_size parameters
        # pylint: disable=too-many-boolean-expressions
        if ((self.size.value is not None and str(self.size.value).endswith("%"))
                or (self.scm_size.value is not None
                    and str(self.scm_size.value).endswith("%"))
                or (self.nvme_size.value is not None
                    and str(self.nvme_size.value).endswith("%"))):
            index = self.server_index.value
            try:
                params = test.server_managers[index].autosize_pool_params(
                    size=self.size.value,
                    tier_ratio=self.tier_ratio.value,
                    scm_size=self.scm_size.value,
                    nvme_size=self.nvme_size.value,
                    min_targets=self.min_targets.value,
                    quantity=self.quantity.value)
            except ServerFailed as error:
                test.fail(
                    "Failure autosizing pool parameters: {}".format(error))
            except AutosizeCancel as error:
                test.cancel(error)

            # Update the pool parameters with any autosized values
            for name in params:
                test_pool_param = getattr(self, name)
                test_pool_param.update(params[name], name)

                # Cache the autosized value so we do not calculate it again
                # pylint: disable=protected-access
                cache_id = (name, self.namespace, test_pool_param._default)
                test.params._cache[cache_id] = params[name]

        # Use a unique pool label if using pool labels
        if self.label.value is not None:
            if not isinstance(self.label_generator, LabelGenerator):
                raise CommandFailure(
                    "Unable to create a unique pool label; " +\
                        "Undefined label_generator")
            self.label.update(self.label_generator.get_label(self.label.value))

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
            "size": self.size.value,
            "tier_ratio": self.tier_ratio.value,
            "scm_size": self.scm_size.value,
            "properties": self.properties.value,
            "label": self.label.value
        }
        for key in ("target_list", "svcn", "nvme_size"):
            value = getattr(self, key).value
            if value is not None:
                kwargs[key] = value

        if self.control_method.value == self.USE_API:
            raise CommandFailure(
                "Error: control method {} not supported for create()".format(
                    self.control_method.value))

        if self.control_method.value == self.USE_DMG and self.dmg:
            # Create a pool with the dmg command and store its CmdResult
            self._log_method("dmg.pool_create", kwargs)
            data = self.dmg.pool_create(**kwargs)

            if self.dmg.result.exit_status == 0:
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
                    raise CommandFailure(
                        "Error: control method {} not supported for "
                        "create()".format(self.control_method.value))

                if self.control_method.value == self.USE_DMG and self.dmg:
                    # Destroy the pool with the dmg command. First, check the
                    # flag and see if the caller wants to use the label or UUID.
                    # If the caller want to use the label, check to make sure
                    # that self.label is set.
                    if self.use_label and self.label.value is not None:
                        self.dmg.pool_destroy(
                            pool=self.label.value, force=force)
                    else:
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

    @fail_on(CommandFailure)
    def evict(self):
        """Evict all pool connections to a DAOS pool."""
        if self.pool:
            self.log.info("Evict all pool connections for pool: %s", self.uuid)

            if self.control_method.value == self.USE_DMG and self.dmg:
                self.dmg.pool_evict(self.uuid)

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
            for key, val in list(locals().items())
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
            for key, val in list(locals().items())
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
            for key, val in list(locals().items())
            if key != "self" and val is not None]
        return self._check_info(checks)

    def rebuild_complete(self):
        """Determine if the pool rebuild is complete.

        Returns:
            bool: True if pool rebuild is complete; False otherwise

        """
        status = False
        if self.control_method.value == self.USE_API:
            self.display_pool_rebuild_status()
            status = self.info.pi_rebuild_st.rs_done == 1
        elif self.control_method.value == self.USE_DMG and self.dmg:
            self.set_query_data()
            self.log.info(
                "Pool %s query data: %s\n", self.uuid, self.query_data)
            status = self.query_data["response"]["rebuild"]["state"] == "done"
        elif self.control_method.value == self.USE_DMG:
            self.log.error("Error: Undefined dmg command")
        else:
            self.log.error(
                "Error: Undefined control_method: %s",
                self.control_method.value)
        return status

    def wait_for_rebuild(self, to_start, interval=1):
        """Wait for the rebuild to start or end.

        Args:
            to_start (bool): whether to wait for rebuild to start or end
            interval (int): number of seconds to wait in between rebuild
                completion checks
        """
        start = time()
        self.log.info(
            "Waiting for rebuild to %s%s ...",
            "start" if to_start else "complete",
            " with a {} second timeout".format(self.rebuild_timeout.value)
            if self.rebuild_timeout.value is not None else "")

        start = time()
        while self.rebuild_complete() == to_start:
            self.log.info(
                "  Rebuild %s ...",
                "has not yet started" if to_start else "in progress")
            if self.rebuild_timeout.value is not None:
                if time() - start > self.rebuild_timeout.value:
                    raise DaosTestError(
                        "TIMEOUT detected after {} seconds while for waiting "
                        "for rebuild to {}.  This timeout can be adjusted via "
                        "the 'pool/rebuild_timeout' test yaml "
                        "parameter.".format(
                            self.rebuild_timeout.value,
                            "start" if to_start else "complete"))
            sleep(interval)

        self.log.info(
            "Rebuild %s detected", "start" if to_start else "completion")

    @fail_on(DaosApiError)
    @fail_on(CommandFailure)
    def exclude(self, ranks, daos_log=None, tgt_idx=None):
        """Manually exclude a rank from this pool.

        Args:
            ranks (list): a list daos server ranks (int) to exclude
            daos_log (DaosLog): object for logging messages
            tgt_idx (string): str of targets to exclude on ranks ex: "1,2"

        Returns:
            bool: True if the ranks were excluded from the pool; False if the
                pool is undefined

        """
        status = False
        if self.control_method.value == self.USE_API:
            msg = "Excluding server ranks {} from pool {}".format(
                ranks, self.uuid)
            self.log.info(msg)
            if daos_log is not None:
                daos_log.info(msg)
            self._call_method(self.pool.exclude, {"rank_list": ranks})
            status = True

        elif self.control_method.value == self.USE_DMG and self.dmg:
            self.dmg.pool_exclude(self.uuid, ranks, tgt_idx)
            status = True

        return status

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
            self.log.info(
                "Rebuild completed before all the written data could be read - "
                "Currently not reporting this as an error.")
            # status = False
        elif not status:
            self.log.error("Errors detected reading data during rebuild")
        return status

    @fail_on(CommandFailure)
    def set_query_data(self):
        """Execute dmg pool query and store the results.

        Only supported with the dmg control method.
        """
        self.query_data = {}
        if self.pool:
            if self.dmg:
                uuid = self.pool.get_uuid_str()
                end_time = None
                if self.pool_query_timeout.value is not None:
                    self.log.info(
                        "Waiting for pool %s query to be responsive with a %s "
                        "second timeout", uuid, self.pool_query_timeout.value)
                    end_time = time() + self.pool_query_timeout.value
                while True:
                    try:
                        self.query_data = self.dmg.pool_query(uuid)
                        break
                    except CommandFailure as error:
                        if end_time is not None:
                            self.log.info(
                                "Pool %s query still non-responsive: %s",
                                uuid, str(error))
                            if time() > end_time:
                                raise CommandFailure(
                                    "TIMEOUT detected after {} seconds while "
                                    "waiting for pool {} query response. This "
                                    "timeout can be adjusted via the "
                                    "'pool/pool_query_timeout' test yaml "
                                    "parameter.".format(
                                        uuid, self.pool_query_timeout.value)) \
                                            from error
                        else:
                            raise CommandFailure(error) from error
            else:
                self.log.error("Error: Undefined dmg command")

    @fail_on(CommandFailure)
    def reintegrate(self, rank, tgt_idx=None):
        """Use dmg to reintegrate the rank and targets into this pool.

        Only supported with the dmg control method.
        Args:
            rank (str): daos server rank to reintegrate
            tgt_idx (string): str of targets to reintegrate on ranks ex: "1,2"

        Returns:
            bool: True if the rank was reintegrated into the pool; False if the
            reintegrate failed

        """
        status = False
        self.dmg.pool_reintegrate(self.uuid, rank, tgt_idx)
        status = True

        return status

    @fail_on(CommandFailure)
    def drain(self, rank, tgt_idx=None):
        """Use dmg to drain the rank and targets from this pool.

        Only supported with the dmg control method.
        Args:
            rank (str): daos server rank to drain
            tgt_idx (string): str of targets to drain on ranks ex: "1,2"

        Returns:
            bool: True if the rank was drained from the pool; False if the
            reintegrate failed

        """
        status = False

        self.dmg.pool_drain(self.uuid, rank, tgt_idx)
        status = True

        return status


class LabelGenerator():
    # pylint: disable=too-few-public-methods
    """Generates label used for pool."""

    def __init__(self, value=1):
        """Constructor.

        Args:
            value (int): Number that's attached after the base_label.

        """
        self.value = value

    def get_label(self, base_label):
        """Create a label by adding number after the given base_label.

        Args:
            base_label (str): Label prefix. Don't include space.

        Returns:
            str: Created label.

        """
        label = base_label
        if label is not None:
            label = "_".join([base_label, str(self.value)])
            self.value += 1
        return label

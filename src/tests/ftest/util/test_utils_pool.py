#!/usr/bin/python
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
from time import sleep, time
import ctypes
import json

from test_utils_base import TestDaosApiBase, LabelGenerator
from avocado import fail_on
from command_utils import BasicParameter
from exception_utils import CommandFailure
from pydaos.raw import (DaosApiError, DaosPool, c_uuid_to_str, daos_cref)
from general_utils import check_pool_files, DaosTestError
from server_utils_base import ServerFailed, AutosizeCancel
from dmg_utils import DmgCommand


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
        self.nranks = BasicParameter(None)
        self.size = BasicParameter(None)
        self.tier_ratio = BasicParameter(None)
        self.scm_size = BasicParameter(None)
        self.nvme_size = BasicParameter(None)
        self.prop_name = BasicParameter(None)       # name of property to be set
        self.prop_value = BasicParameter(None)      # value of property
        self.properties = BasicParameter(None)      # string of cs name:value
        self.rebuild_timeout = BasicParameter(None)
        self.pool_query_timeout = BasicParameter(None)
        self.acl_file = BasicParameter(None)
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
        self.info = None
        self.svc_ranks = None
        self.connected = False
        # Flag to allow the non-create operations to use UUID. e.g., if you want
        # to destroy the pool with UUID, set this to False, then call destroy().
        self.use_label = True

        self._dmg = None
        self.dmg = dmg_command

        self.query_data = []

        self.scm_per_rank = None
        self.nvme_per_rank = None

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
        if ((self.scm_size.value is not None
             and str(self.scm_size.value).endswith("%"))
                or (self.nvme_size.value is not None
                    and str(self.nvme_size.value).endswith("%"))):
            index = self.server_index.value
            try:
                params = test.server_managers[index].autosize_pool_params(
                    size=None,
                    tier_ratio=None,
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
                    "Unable to create a unique pool label; Undefined label_generator")
            self.label.update(self.label_generator.get_label(self.label.value))

    @property
    def uuid(self):
        """Get the pool UUID.

        Returns:
            str: pool UUID

        """
        uuid = None
        if self.pool and self.pool.uuid:
            uuid = self.pool.get_uuid_str()
        return uuid

    @uuid.setter
    def uuid(self, value):
        """Set the pool UUID.

        Args:
            value (str): pool UUID
        """
        if self.pool:
            self.pool.set_uuid_str(value)

    @property
    def identifier(self):
        """Get the pool uuid or label.

        Returns:
            str: pool label if using labels and one is defined; otherwise the
                pool uuid

        """
        identifier = self.uuid
        if self.use_label and self.label.value is not None:
            identifier = self.label.value
        return identifier

    @property
    def dmg(self):
        """Get the DmgCommand object.

        Returns:
            DmgCommand: the dmg command object assigned to this class

        """
        return self._dmg

    @dmg.setter
    def dmg(self, value):
        """Set the DmgCommand object.

        Args:
            value (DmgCommand): dmg command object to use with this class

        Raises:
            TypeError: Raised if value is not DmgCommand object.

        """
        if not isinstance(value, DmgCommand):
            raise TypeError("Invalid 'dmg' object type: {}".format(type(value)))
        self._dmg = value

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
            "nranks": self.nranks.value,
            "properties": self.properties.value,
            "acl_file": self.acl_file.value,
            "label": self.label.value
        }
        for key in ("target_list", "svcn", "nvme_size"):
            value = getattr(self, key).value
            if value is not None:
                kwargs[key] = value

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
            rank = rank_t(*service_replicas)
            rl_ranks = ctypes.POINTER(ctypes.c_uint)(rank)
            self.pool.svc = daos_cref.RankList(
                rl_ranks, len(service_replicas))

            # Set UUID and attached to the DaosPool object
            self.uuid = data["uuid"]
            self.pool.attached = 1

            # Set effective size of mediums per rank
            self.scm_per_rank = data["scm_per_rank"]
            self.nvme_per_rank = data["nvme_per_rank"]

        # Set the TestPool attributes for the created pool
        if self.pool.attached:
            self.svc_ranks = [
                int(self.pool.svc.rl_ranks[index])
                for index in range(self.pool.svc.rl_nr)]

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
                self.log.info("Destroying pool %s", self.identifier)

                # Destroy the pool with the dmg command.
                self.dmg.pool_destroy(pool=self.identifier, force=force)
                status = True

            self.pool = None
            self.info = None
            self.svc_ranks = None

        return status

    @fail_on(CommandFailure)
    def set_property(self, prop_name=None, prop_value=None):
        """Set Property.

        It sets property for a given pool uuid using dmg.

        Args:
            prop_name (str, optional): pool property name. Defaults to
                None, which uses the TestPool.prop_name.value
            prop_value (str, optional): value to be set for the property.
                Defaults to None, which uses the TestPool.prop_value.value
        """
        if self.pool:
            self.log.info("Set-prop for Pool: %s", self.identifier)

            # If specific values are not provided, use the class values
            if prop_name is None:
                prop_name = self.prop_name.value
            if prop_value is None:
                prop_value = self.prop_value.value
            self.dmg.pool_set_prop(self.identifier, prop_name, prop_value)

    @fail_on(CommandFailure)
    def get_property(self, prop_name):
        """Get Property.

        It gets property for a given pool uuid using dmg.

        Args:
            prop_name (str): Name of the pool property.

        Returns:
            prop_value (str): Return pool property value.

        """
        prop_value = ""
        if self.pool:
            self.log.info("Get-prop for Pool: %s", self.identifier)

            # If specific property are not provided, get all the property
            self.dmg.pool_get_prop(self.identifier, prop_name)

            if self.dmg.result.exit_status == 0:
                prop_value = json.loads(
                    self.dmg.result.stdout)['response'][0]['value']

        return prop_value

    @fail_on(CommandFailure)
    def evict(self):
        """Evict all pool connections to a DAOS pool."""
        if self.pool:
            self.log.info(
                "Evict all pool connections for pool: %s", self.identifier)

            self.dmg.pool_evict(self.identifier)

    @fail_on(DaosApiError)
    def get_info(self):
        """Query the pool for information.

        Sets the self.info attribute.
        """
        if self.pool:
            self.connect()
            self.log.info("Querying pool %s", self.identifier)
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
                checks.append((key, getattr(self.info.pi_space, key), val))
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

    def check_free_space(self, expected_scm=None, expected_nvme=None,
                         timeout=30):
        """Check pool free space with expected value.
        Args:
            expected_scm (int, optional): pool expected SCM free space.
            expected_nvme (int, optional): pool expected NVME free space.
            timeout(int, optional): time to fail test if it could not match
                expected values.
        Note:
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
            default '=='.
        """
        if not expected_scm and not expected_nvme:
            self.log.error("at least one space parameter must be specified")
            return False

        done = False
        scm_fs = 0
        nvme_fs = 0
        start = time()
        scm_index, nvme_index = 0, 1
        while time() - start < timeout and not done:
            sleep(1)
            checks = []
            self.get_info()
            scm_fs = self.info.pi_space.ps_space.s_free[scm_index]
            nvme_fs = self.info.pi_space.ps_space.s_free[nvme_index]
            if expected_scm is not None:
                checks.append(("scm", scm_fs, expected_scm))
            if expected_nvme is not None:
                checks.append(("nvme", nvme_fs, expected_nvme))
            done = self._check_info(checks)

        if not done:
            raise DaosTestError(
                "Pool Free space did not match: actual={},{} expected={},{}".format(
                scm_fs, nvme_fs, expected_scm, expected_nvme))

        return done

    def check_rebuild_status(self, rs_version=None, rs_seconds=None,
                             rs_errno=None, rs_state=None, rs_padding32=None,
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
            rs_state (int, optional): rebuild state flag. Defaults to None.
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
            status = self.info.pi_rebuild_st.rs_state == 2
        elif self.control_method.value == self.USE_DMG:
            self.set_query_data()
            self.log.info(
                "Pool %s query data: %s\n", self.uuid, self.query_data)
            status = self.query_data["response"]["rebuild"]["state"] == "done"
        else:
            self.log.error(
                "Error: Undefined control_method: %s",
                self.control_method.value)

        return status

    def get_rebuild_state(self, verbose=True):
        """Get the rebuild state from the dmg pool query.

        Args:
            verbose (bool, optional): whether to display the rebuild data. Defaults to True.

        Returns:
            [type]: [description]
        """
        self.set_query_data()
        try:
            if verbose:
                self.log.info(
                    "Pool %s query rebuild data: %s\n",
                    self.uuid, self.query_data["response"]["rebuild"])
            return self.query_data["response"]["rebuild"]["state"]
        except KeyError as error:
            self.log.error("Unable to detect rebuild state: %s", error)
            return None

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

        # Expect the state to be 'busy' or 'done' when waiting for rebuild to start or 'done' when
        # waiting for rebuild to complete.
        expected_states = ["busy", "done"] if to_start else ["done"]

        start = time()
        while self.get_rebuild_state() not in expected_states:
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

    @fail_on(CommandFailure)
    def exclude(self, ranks, tgt_idx=None):
        """Manually exclude a rank from this pool.

        Args:
            ranks (list): a list daos server ranks (int) to exclude
            tgt_idx (string, optional): str of targets to exclude on ranks
                ex: "1,2". Defaults to None.
        """
        self.dmg.pool_exclude(self.identifier, ranks, tgt_idx)

    def check_files(self, hosts):
        """Check if pool files exist on the specified list of hosts.

        Args:
            hosts (list): list of hosts

        Returns:
            bool: True if the files for this pool exist on each host; False
                otherwise

        """
        return check_pool_files(self.log, hosts, self.uuid.lower())

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
            "rs_version", "rs_padding32", "rs_errno", "rs_state",
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
    def set_query_data(self, show_enabled=False, show_disabled=False):
        """Execute dmg pool query and store the results.

        Args:
            show_enabled (bool, optional): Display enabled ranks.
            show_disabled (bool, optional): Display disabled ranks.

        Only supported with the dmg control method.
        """
        self.query_data = {}
        if self.pool:
            if self.dmg:
                end_time = None
                if self.pool_query_timeout.value is not None:
                    self.log.info(
                        "Waiting for pool %s query to be responsive with a %s "
                        "second timeout", self.identifier,
                        self.pool_query_timeout.value)
                    end_time = time() + self.pool_query_timeout.value
                while True:
                    try:
                        self.query_data = self.dmg.pool_query(self.identifier, show_enabled,
                                show_disabled)
                        break
                    except CommandFailure as error:
                        if end_time is not None:
                            self.log.info(
                                "Pool %s query still non-responsive: %s",
                                self.identifier, str(error))
                            if time() > end_time:
                                raise CommandFailure(
                                    "TIMEOUT detected after {} seconds while "
                                    "waiting for pool {} query response. This "
                                    "timeout can be adjusted via the "
                                    "'pool/pool_query_timeout' test yaml "
                                    "parameter.".format(
                                        self.pool_query_timeout.value,
                                        self.identifier)) \
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
            tgt_idx (str, optional): string of targets to reintegrate on ranks
            ex: "1,2". Defaults to None.
        """
        self.dmg.pool_reintegrate(self.identifier, rank, tgt_idx)

    @fail_on(CommandFailure)
    def drain(self, rank, tgt_idx=None):
        """Use dmg to drain the rank and targets from this pool.

        Only supported with the dmg control method.

        Args:
            rank (str): daos server rank to drain
            tgt_idx (str, optional): string of targets to drain on ranks
                ex: "1,2". Defaults to None.
        """
        self.dmg.pool_drain(self.identifier, rank, tgt_idx)

    def get_acl(self):
        """Get ACL from a DAOS pool.

        Returns:
            str: dmg pool get-acl output.

        """
        return self.dmg.pool_get_acl(pool=self.identifier)

    def update_acl(self, use_acl, entry=None):
        """Update ACL for a DAOS pool.

        Can't use both ACL file and entry, so use_acl = True and entry != None
        isn't allowed.

        Args:
            use_acl (bool): Whether to use the ACL file during the update.
            entry (str, optional): entry to be updated.
        """
        acl_file = None
        if use_acl:
            acl_file = self.acl_file.value
        self.dmg.pool_update_acl(
            pool=self.identifier, acl_file=acl_file, entry=entry)

    def delete_acl(self, principal):
        """Delete ACL from a DAOS pool.

        Args:
            principal (str): principal to be deleted
        """
        self.dmg.pool_delete_acl(pool=self.identifier, principal=principal)

    def overwrite_acl(self):
        """Overwrite ACL in a DAOS pool."""
        if self.acl_file.value:
            self.dmg.pool_overwrite_acl(
                pool=self.identifier, acl_file=self.acl_file.value)
        else:
            self.log.error("self.acl_file isn't defined!")

    def measure_rebuild_time(self, operation, interval=1):
        """Measure rebuild time.

        This method is mainly for debugging purpose. We'll analyze the output when we
        realize that rebuild is taking too long.

        Args:
            operation (str): Type of operation to print in the log.
            interval (int): Interval (sec) to call pool query to check the rebuild status.
                Defaults to 1.
        """
        start = float(time())
        self.wait_for_rebuild(to_start=True, interval=interval)
        self.wait_for_rebuild(to_start=False, interval=interval)
        duration = float(time()) - start
        self.log.info("%s duration: %.1f sec", operation, duration)

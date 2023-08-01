"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines
import os
from time import sleep, time
import ctypes
import json

from avocado import fail_on, TestFail
from pydaos.raw import (DaosApiError, DaosPool, c_uuid_to_str, daos_cref)

from test_utils_base import TestDaosApiBase, LabelGenerator
from command_utils import BasicParameter
from exception_utils import CommandFailure
from general_utils import check_pool_files, DaosTestError
from dmg_utils import DmgCommand, DmgJsonCommandFailure

POOL_NAMESPACE = "/run/pool/*"
POOL_TIMEOUT_INCREMENT = 200


def add_pool(test, namespace=POOL_NAMESPACE, create=True, connect=True, dmg=None, **params):
    """Add a new TestPool object to the test.

    Args:
        test (Test): the test to which the pool will be added
        namespace (str, optional): TestPool parameters path in the test yaml file. Defaults to
            POOL_NAMESPACE.
        create (bool, optional): should the pool be created. Defaults to True.
        connect (bool, optional): should the pool be connected. Defaults to True.
        dmg (DmgCommand, optional): dmg command used to create the pool. Defaults to None, which
            calls test.get_dmg_command().

    Returns:
        TestPool: the new pool object

    """
    if not dmg:
        dmg = test.get_dmg_command()
    pool = TestPool(
        namespace=namespace, context=test.context, dmg_command=dmg,
        label_generator=test.label_generator)
    pool.get_params(test)
    if params:
        pool.update_params(**params)
    if create:
        pool.create()
    if create and connect:
        pool.connect()

    # Add a step to remove this pool when the test completes and ensure their is enough time for the
    # pool destroy to be attempted - accounting for a possible dmg command timeout
    test.increment_timeout(POOL_TIMEOUT_INCREMENT)
    test.register_cleanup(remove_pool, test=test, pool=pool)

    return pool


def remove_pool(test, pool):
    """Remove the requested pool from the test.

    Args:
        test (Test): the test from which to destroy the pool
        pool (TestPool): the pool to destroy

    Returns:
        list: a list of any errors detected when removing the pool

    """
    error_list = []
    test.test_log.info("Destroying pool %s", pool.identifier)

    # Ensure exceptions are raised for any failed command
    exit_status_exception = None
    if pool.dmg is not None:
        exit_status_exception = pool.dmg.exit_status_exception
        pool.dmg.exit_status_exception = True

    # Attempt to destroy the pool
    try:
        pool.destroy(force=1, disconnect=1, recursive=1)
    except (DaosApiError, TestFail) as error:
        test.test_log.info("  {}".format(error))
        error_list.append("Error destroying pool {}: {}".format(pool.identifier, error))

    # Restore raising exceptions for any failed command
    if exit_status_exception is False:
        pool.dmg.exit_status_exception = exit_status_exception

    return error_list


def get_size_params(pool):
    """Get the TestPool params that can be used to create a pool of the same size.

    Useful for creating multiple pools of equal --size=X% as each subsequent 'dmg pool create
    --size=X%' results in a smaller pool each time due to using a percentage of the available free
    space.

    Args:
        pool (TestPool): pool whose size is being replicated.

    Returns:
        dict: size params argument for an add_pool() method

    """
    return {"size": None,
            "tier_ratio": None,
            "scm_size": pool.scm_per_rank,
            "nvme_size": pool.nvme_per_rank}


def check_pool_creation(test, pools, max_duration, offset=1, durations=None):
    """Check the duration of each pool creation meets the requirement.

    Args:
        test (Test): the test to fail if the pool creation exceeds the max duration
        pools (list): list of TestPool objects to create
        max_duration (int): max pool creation duration allowed in seconds
        offset (int, optional): pool index offset. Defaults to 1.
        durations (list, optional): list of other pool create durations to include in the check.
            Defaults to None.
    """
    if durations is None:
        durations = []
    for index, pool in enumerate(pools):
        durations.append(time_pool_create(test.log, index + offset, pool))

    exceeding_duration = 0
    for index, duration in enumerate(durations):
        if duration > max_duration:
            exceeding_duration += 1

    if exceeding_duration:
        test.fail(
            "Pool creation took longer than {} seconds on {} pool(s)".format(
                max_duration, exceeding_duration))


def time_pool_create(log, number, pool):
    """Time how long it takes to create a pool.

    Args:
        log (logger): logger for the messages produced by this method
        number (int): pool number in the list
        pool (TestPool): pool to create

    Returns:
        float: number of seconds elapsed during pool create

    """
    start = time()
    pool.create()
    duration = time() - start
    log.info("Pool %s creation: %s seconds", number, duration)
    return duration


class TestPool(TestDaosApiBase):
    # pylint: disable=too-many-public-methods,too-many-instance-attributes
    """A class for functional testing of DaosPools objects."""

    def __init__(self, context, dmg_command, label_generator=None, namespace=POOL_NAMESPACE):
        # pylint: disable=unused-argument
        """Initialize a TestPool object.

        Args:
            context (DaosContext): The daos environment and other info. Use
                self.context when calling from a test.
            dmg_command (DmgCommand): DmgCommand used to call dmg command. This
                value can be obtained by calling self.get_dmg_command() from a
                test. It'll return the object with -l <Access Point host:port>
                and --insecure.
            label_generator (LabelGenerator, optional): Generates label by
                adding number to the end of the prefix set in self.label.
                There's a link between label_generator and label. If the label
                is used as it is, i.e., not None, label_generator must be
                provided in order to call create(). Defaults to None.
            namespace (str, optional): path to test yaml parameters. Defaults to POOL_NAMESPACE.
        """
        super().__init__(namespace)
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
        self.pool_query_delay = BasicParameter(None)
        self.acl_file = BasicParameter(None)
        self.label = BasicParameter(None, "TestPool")
        self.label_generator = label_generator

        # Optional TestPool parameters used to autosize the dmg pool create
        # 'size', 'scm_size', and/or 'nvme_size' values:
        #   server_index: TestWithServers.server_managers list index
        #   quantity:     number of pools to account for in sizing
        #   min_targets:  minimum number of targets allowed
        self.server_index = BasicParameter(None, 0)
        self.quantity = BasicParameter(None, 1)
        self.min_targets = BasicParameter(None, 1)

        # Parameter to control log mask enable/disable for pool create/destroy
        self.set_logmasks = BasicParameter(None, True)

        self.pool = None
        self.info = None
        self.svc_ranks = None
        self.connected = False
        # Flag to allow the non-create operations to use UUID. e.g., if you want
        # to destroy the pool with UUID, set this to False, then call destroy().
        self.use_label = True

        self._dmg = None
        self.dmg = dmg_command

        self.query_data = {}

        self.scm_per_rank = None
        self.nvme_per_rank = None

        # Current rebuild data used when determining if pool rebuild is running or complete
        self._rebuild_data = {}
        self._reset_rebuild_data()

    def __str__(self):
        """Return the pool label (if defined) and UUID identification.

        Returns:
            str: the pool label (if defined) and UUID identification

        """
        if self.use_label and self.label.value is not None:
            return "Pool {} ({})".format(self.label.value, self.uuid)
        return "Pool {}".format(self.uuid)

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Autosize any size/scm_size/nvme_size parameter whose value ends in "%".
        Also create a unique label by adding the incremented number prefix.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)

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
        try:
            return self.pool.get_uuid_str()
        except (AttributeError, TypeError):
            return None
        except IndexError:
            return self.pool.uuid

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

    def skip_cleanup(self):
        """Prevent pool from being removed during cleanup.

        Useful for corner case tests where the pool no longer exists due to a storage format.
        """
        self.connected = False
        if self.pool:
            self.pool.attached = False

    @fail_on(CommandFailure)
    @fail_on(DaosApiError)
    @fail_on(DmgJsonCommandFailure)
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
        # Elevate engine log_mask to DEBUG before, then restore after pool create
        self._log_method("dmg.pool_create", kwargs)
        if self.set_logmasks.value is True:
            self.dmg.server_set_logmasks("DEBUG", raise_exception=False)
        try:
            data = self.dmg.pool_create(**kwargs)
            create_res = self.dmg.result
        finally:
            if self.set_logmasks.value is True:
                self.dmg.server_set_logmasks(raise_exception=False)

        # make sure dmg exit status is that of the pool create, not the set-logmasks
        if data is not None and create_res is not None:
            self.dmg.result = create_res

        if data and data["status"] == 0:
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
    def destroy(self, force=1, disconnect=1, recursive=1):
        """Destroy the pool with either API or dmg.

        It uses control_method member previously set, so if you want to use the
        other way for some reason, update it before calling this method.

        Args:
            force (int, optional): force flag. Defaults to 1.
            disconnect (int, optional): disconnect flag. Defaults to 1.
            recursive (int, optional): recursive flag. Defaults to 1.

        Returns:
            bool: True if the pool has been destroyed; False if the pool is not
                defined.

        """
        status = False
        if self.pool:
            if disconnect:
                self.log.info("Disconnecting from pool %s", self.identifier)
                self.disconnect()
            if self.pool.attached:
                self.log.info("Destroying pool %s", self.identifier)

                # Destroy the pool with the dmg command.
                # Elevate log_mask to DEBUG, then restore after pool destroy
                if self.set_logmasks.value is True:
                    self.dmg.server_set_logmasks("DEBUG", raise_exception=False)
                    self.dmg.pool_destroy(pool=self.identifier, force=force, recursive=recursive)
                    self.dmg.server_set_logmasks(raise_exception=False)
                else:
                    self.dmg.pool_destroy(pool=self.identifier, force=force, recursive=recursive)

                status = True

            self.pool = None
            self.info = None
            self.svc_ranks = None

        return status

    def delete_acl(self, principal):
        """Delete ACL from a DAOS pool.

        Args:
            principal (str): principal to be deleted

        Raises:
            CommandFailure: if the dmg pool delete-acl command fails.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self.dmg.pool_delete_acl(pool=self.identifier, principal=principal)

    @fail_on(CommandFailure)
    def drain(self, rank, tgt_idx=None):
        """Use dmg to drain the rank and targets from this pool.

        Only supported with the dmg control method.

        Args:
            rank (str): daos server rank to drain
            tgt_idx (str, optional): targets to drain on ranks, ex: "1,2". Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self.dmg.pool_drain(self.identifier, rank, tgt_idx)

    @fail_on(CommandFailure)
    def disable_aggregation(self):
        """ Disable pool aggregation."""
        self.log.info("Disable pool aggregation for %s", str(self))
        self.set_property("reclaim", "disabled")

    @fail_on(CommandFailure)
    def enable_aggregation(self):
        """ Enable pool aggregation."""
        self.log.info("Enable pool aggregation for %s", str(self))
        self.set_property("reclaim", "time")

    @fail_on(CommandFailure)
    def evict(self):
        """Evict all pool connections to a DAOS pool."""
        if self.pool:
            self.log.info("Evict all pool connections for pool: %s", self.identifier)
            try:
                self.dmg.pool_evict(self.identifier)
            finally:
                self.connected = False

    @fail_on(CommandFailure)
    def exclude(self, ranks, tgt_idx=None):
        """Manually exclude a rank from this pool.

        Args:
            ranks (list): a list daos server ranks (int) to exclude
            tgt_idx (string, optional): targets to exclude on ranks, ex: "1,2". Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self.dmg.pool_exclude(self.identifier, ranks, tgt_idx)

    @fail_on(CommandFailure)
    def extend(self, ranks):
        """Extend the pool to additional ranks.

        Args:
            ranks (str): comma separate list of daos server ranks (int) to extend

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self.dmg.pool_extend(self.identifier, ranks)

    def get_acl(self):
        """Get ACL from a DAOS pool.

        Raises:
            CommandFailure: if the dmg pool get-acl command fails.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self.dmg.pool_get_acl(pool=self.identifier)

    @fail_on(CommandFailure)
    def set_prop(self, *args, **kwargs):
        """Get pool properties by calling dmg pool set-prop.

        Args:
            args (tuple, optional): positional arguments to DmgCommand.pool_set_prop
            kwargs (dict, optional): named arguments to DmgCommand.pool_set_prop

        Raises:
            TestFailure: if there is an error running dmg pool set-prop

        Returns:
            dict: json output of dmg pool set-prop command

        """
        return self.dmg.pool_set_prop(pool=self.identifier, *args, **kwargs)

    @fail_on(CommandFailure)
    def get_prop(self, *args, **kwargs):
        """Get pool properties by calling dmg pool get-prop.

        Args:
            args (tuple, optional): positional arguments to DmgCommand.pool_get_prop
            kwargs (dict, optional): named arguments to DmgCommand.pool_get_prop

        Raises:
            TestFailure: if there is an error running dmg pool get-prop

        Returns:
            dict: json output of dmg pool get-prop command

        """
        return self.dmg.pool_get_prop(self.identifier, *args, **kwargs)

    def get_prop_values(self, *args, **kwargs):
        """Get pool property values from the dmg pool get-prop json output.

        Args:
            args (tuple, optional): positional arguments to DmgCommand.pool_get_prop
            kwargs (dict, optional): named arguments to DmgCommand.pool_get_prop

        Raises:
            TestFailure: if there is an error running dmg pool get-prop

        Returns:
            list: a list of values matching the or specified property names.

        """
        values = []
        self.log.info("Getting property values for %s", self)
        data = self.get_prop(*args, **kwargs)
        if data['status'] != 0:
            return values
        for entry in data['response']:
            values.append(entry['value'])
        return values

    @fail_on(CommandFailure)
    def get_property(self, prop_name):
        """Get the pool property with the specified name.

        Args:
            prop_name (str): Name of the pool property.

        Returns:
            str: pool property value.

        """
        prop_value = ""
        if self.pool:
            self.log.info("Get-prop for Pool: %s", self.identifier)

            # If specific property are not provided, get all the property
            self.dmg.pool_get_prop(self.identifier, prop_name)

            if self.dmg.result.exit_status == 0:
                prop_value = json.loads(self.dmg.result.stdout)['response'][0]['value']

        return prop_value

    def overwrite_acl(self):
        """Overwrite ACL in a DAOS pool.

        Raises:
            CommandFailure: if the dmg pool overwrite-acl command fails.

        """
        if self.acl_file.value:
            self.dmg.pool_overwrite_acl(pool=self.identifier, acl_file=self.acl_file.value)
        else:
            self.log.error("self.acl_file isn't defined!")

    @fail_on(CommandFailure)
    def query(self, show_enabled=False, show_disabled=False):
        """Execute dmg pool query.

        Args:
            show_enabled (bool, optional): Display enabled ranks.
            show_disabled (bool, optional): Display disabled ranks.

        Returns:
            dict: the dmg json command output converted to a python dictionary

        """
        end_time = None
        if self.pool_query_timeout.value is not None:
            self.log.info(
                "Waiting for pool %s query to be responsive with a %s second timeout",
                self.identifier, self.pool_query_timeout.value)
            end_time = time() + self.pool_query_timeout.value

        while True:
            try:
                return self.dmg.pool_query(self.identifier, show_enabled, show_disabled)

            except CommandFailure as error:
                if end_time is None:
                    raise

                self.log.info("Pool %s query still non-responsive: %s", self.identifier, str(error))
                if time() > end_time:
                    raise CommandFailure(
                        "TIMEOUT detected after {} seconds while waiting for pool {} query "
                        "response. This timeout can be adjusted via the 'pool/pool_query_timeout' "
                        "test yaml parameter.".format(
                            self.pool_query_timeout.value, self.identifier)) from error

                if self.pool_query_delay:
                    self.log.info(
                        "Waiting %s seconds before issuing next dmg pool query",
                        self.pool_query_delay)
                    sleep(self.pool_query_delay.value)

    @fail_on(CommandFailure)
    def query_targets(self, *args, **kwargs):
        """Call dmg pool query-targets.

        Args:
            args (tuple, optional): positional arguments to DmgCommand.pool_query_targets
            kwargs (dict, optional): named arguments to DmgCommand.pool_query_targets

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self.dmg.pool_query_targets(self.identifier, *args, **kwargs)

    @fail_on(CommandFailure)
    def reintegrate(self, rank, tgt_idx=None):
        """Use dmg to reintegrate the rank and targets into this pool.

        Args:
            rank (str): daos server rank to reintegrate
            tgt_idx (str, optional): targets to reintegrate on ranks, ex: "1,2". Defaults to None.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self.dmg.pool_reintegrate(self.identifier, rank, tgt_idx)

    @fail_on(CommandFailure)
    def set_property(self, prop_name, prop_value):
        """Set Property.

        It sets property for a given pool uuid using dmg.

        Args:
            prop_name (str): pool property name
            prop_value (str): value to be set for the property
        """
        if self.pool:
            self.log.info("Set-prop for Pool: %s", self.identifier)
            properties = ":".join([prop_name, prop_value])
            self.dmg.pool_set_prop(pool=self.identifier, properties=properties)

    def update_acl(self, use_acl, entry=None):
        """Update ACL for a DAOS pool.

        Can't use both ACL file and entry, so use_acl = True and entry != None
        isn't allowed.

        Args:
            use_acl (bool): Whether to use the ACL file during the update.
            entry (str, optional): entry to be updated.

        Raises:
            CommandFailure: if the dmg pool update-acl command fails.

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        acl_file = None
        if use_acl:
            acl_file = self.acl_file.value
        return self.dmg.pool_update_acl(pool=self.identifier, acl_file=acl_file, entry=entry)

    def upgrade(self, *args, **kwargs):
        """Call dmg pool upgrade.

        Args:
            args (tuple, optional): positional arguments to DmgCommand.pool_upgrade
            kwargs (dict, optional): named arguments to DmgCommand.pool_upgrade

        Raises:
            CommandFailure: if the command fails.

        Returns:
            dict: json output of the command

        """
        return self.dmg.pool_upgrade(pool=self.identifier, *args, **kwargs)

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

    def check_free_space(self, expected_scm=None, expected_nvme=None, timeout=30):
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

        Raises:
            DaosTestError: if scm or nvme free space doesn't match expected
            values within timeout.

        Returns:
            bool: True if expected value is specified and all the
                specified values match; False if no space parameters specified.

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

    def check_files(self, hosts):
        """Check if pool files exist on the specified list of hosts.

        Args:
            hosts (NodeSet): hosts on which to check files

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

    def get_space_per_target(self, ranks, target_idx):
        """Get space usage per rank, per target using dmg pool query-targets.

        Args:
            ranks (list): List of ranks to be queried
            target_idx (str): Comma-separated list of target idx(s) to be queried

        Returns:
            dict: space per rank, per target
                E.g. {<rank>: {<target>: {scm: {total: X, free: Y, used: Z}, nvme: {...}}}}

        """
        rank_target_tier_space = {}
        for rank in ranks:
            rank_target_tier_space[rank] = {}
            rank_result = self.query_targets(rank=rank, target_idx=target_idx)
            for target, target_info in enumerate(rank_result['response']['Infos']):
                rank_target_tier_space[rank][target] = {}
                for tier in target_info['Space']:
                    rank_target_tier_space[rank][target][tier['media_type']] = {
                        'total': tier['total'],
                        'free': tier['free'],
                        'used': tier['total'] - tier['free']}
        return rank_target_tier_space

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

    def display_space(self):
        """Display the current pool space.

        If the TestPool object has a DmgCommand object assigned, also display
        the free pool space per target.

        """
        if self.dmg:
            # Display all query data
            self.set_query_data()
        else:
            # Display just the space
            self.display_pool_daos_space()

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
        pool_percent = {
            'scm': round(float(daos_space["s_total"][0] - daos_space["s_free"][0])
                         / float(daos_space["s_total"][0]) * 100, 4),
            'nvme': round(float(daos_space["s_total"][1] - daos_space["s_free"][1])
                          / float(daos_space["s_total"][1]) * 100, 4)
        }
        return pool_percent

    def set_query_data(self, show_enabled=False, show_disabled=False):
        """Execute dmg pool query and store the results.

        Args:
            show_enabled (bool, optional): Display enabled ranks.
            show_disabled (bool, optional): Display disabled ranks.

        Raises:
            TestFail: if the dmg pool query command failed

        """
        self.query_data = {}
        self.query_data = self.query(show_enabled, show_disabled)

    def _get_query_data_keys(self, *keys, refresh=False):
        """Get the pool version from the dmg pool query output.

        Args:
            keys (list): dmg pool query dictionary keys to use to access the data
            refresh (bool, optional): whether or not to issue a new dmg pool query before
                collecting the data from its output. Defaults to False.

        Raises:
            CommandFailure: if there was error collecting the dmg pool query data or the keys

        Returns:
            object: the requested dmg pool query data subset

        """
        if not self.query_data or refresh:
            self.set_query_data()
        try:
            value = self.query_data.copy()
            for key in keys:
                value = value[key]
            return value
        except (KeyError, TypeError) as error:
            keys_str = ".".join(map(str, keys))
            raise CommandFailure(
                "The dmg pool query key does not exist: {}".format(keys_str)) from error

    def get_tier_stats(self, refresh=False):
        """Get the pool tier stats from pool query output.

        Args:
             refresh (bool, optional): whether or not to issue a new dmg pool query before
                collecting the data from its output. Defaults to False.

        Returns:
             dict: A dictionary for pool stats, scm and nvme:

        """
        tier_stats = {}
        for tier_stat in self._get_query_data_keys("response", "tier_stats", refresh=refresh):
            tier_type = tier_stat.pop("media_type")
            tier_stats[tier_type] = tier_stat.copy()
        return tier_stats

    def get_total_free_space(self, refresh=False):
        """Get the pool total free space.

        Args:
            refresh (bool, optional): whether or not to issue a new dmg pool query before
                collecting the data from its output. Defaults to False.

        Return:
            total_free_space (int): pool total free space.

        """
        tier_stats = self.get_tier_stats(refresh)
        return sum(stat["free"] for stat in tier_stats.values())

    def get_total_space(self, refresh=False):
        """Get the pool total space.

        Args:
            refresh (bool, optional): whether or not to issue a new dmg pool query before
                collecting the data from its output. Defaults to False.

        Return:
            total_space (int): pool total space.

        """
        tier_stats = self.get_tier_stats(refresh)
        return sum(stat["total"] for stat in tier_stats.values())

    def get_version(self, refresh=False):
        """Get the pool version from the dmg pool query output.

        Args:
            refresh (bool, optional): whether or not to issue a new dmg pool query before
                collecting the data from its output. Defaults to False.

        Raises:
            CommandFailure: if there was error collecting the dmg pool query data or the keys

        Returns:
            int: pool version value

        """
        return int(self._get_query_data_keys("response", "version", refresh=refresh))

    def get_rebuild_status(self, refresh=False):
        """Get the pool rebuild status from the dmg pool query output.

        Args:
            refresh (bool, optional): whether or not to issue a new dmg pool query before
                collecting the data from its output. Defaults to False.

        Raises:
            CommandFailure: if there was error collecting the dmg pool query data or the keys

        Returns:
            int: rebuild status

        """
        return int(self._get_query_data_keys("response", "rebuild", "status", refresh=refresh))

    def get_rebuild_state(self, refresh=True):
        """Get the pool rebuild state from the dmg pool query output.

        Args:
            verbose (bool, optional): whether to display the rebuild data. Defaults to True.
            refresh (bool, optional): whether or not to issue a new dmg pool query before
                collecting the data from its output. Defaults to False.

        Raises:
            CommandFailure: if there was error collecting the dmg pool query data or the keys

        Returns:
            str: rebuild state or None

        """
        return self._get_query_data_keys("response", "rebuild", "state", refresh=refresh)

    def _reset_rebuild_data(self):
        """Reset the rebuild data."""
        self._rebuild_data = {
            "state": None,
            "version": None,
            "status": None,
            "check": None,
            "version_increase": False,
        }
        self.log.info("%s query rebuild data reset: %s", str(self), self._rebuild_data)

    def _update_rebuild_data(self, verbose=True):
        """Update the rebuild data.

        Args:
            verbose (bool, optional): whether to display the pool query info. Defaults to True.
        """
        # Reset the rebuild data if rebuild completion was previously detected
        if self._rebuild_data["check"] == "completed":
            self._reset_rebuild_data()

        # Use the current rebuild data to define the previous rebuild data
        previous_data = dict(self._rebuild_data.items())

        # Update the current rebuild data
        self.set_query_data()
        try:
            self._rebuild_data["version"] = self.get_version(False)
        except (CommandFailure, ValueError) as error:
            self.log.error("Unable to detect the current pool map version: %s", error)
            self._rebuild_data["version"] = None
        try:
            self._rebuild_data["state"] = self.get_rebuild_state(False)
        except CommandFailure as error:
            self.log.error("Unable to detect the current pool rebuild state: %s", error)
            self._rebuild_data["state"] = None
        try:
            self._rebuild_data["status"] = self.get_rebuild_status(False)
        except (CommandFailure, ValueError) as error:
            self.log.error("Unable to detect the current pool rebuild status: %s", error)
            self._rebuild_data["status"] = None

        # Keep track of any map version increases
        if self._rebuild_data["version"] is not None and previous_data["version"] is not None:
            if self._rebuild_data["version"] > previous_data["version"]:
                self._rebuild_data["version_increase"] = True

        # Determine if rebuild is running or completed
        if self._rebuild_data["status"] == 0 and self._rebuild_data["state"] == "done":
            # If the current status is 0 and state is done then rebuild is complete
            self._rebuild_data["check"] = "completed"
        elif self._rebuild_data["status"] == 0 \
                and self._rebuild_data["state"] == "idle" \
                and self._rebuild_data["version_increase"]:
            # If the current status is 0, state is idle, and the map version has increased then
            # rebuild is complete
            self._rebuild_data["check"] = "completed"
        elif self._rebuild_data["state"] == "busy" or previous_data["state"] == "busy":
            # If the current state is busy or idle w/o a version increase after previously being
            # busy then rebuild is running
            self._rebuild_data["check"] = "running"
        elif self._rebuild_data["check"] is None:
            # Otherwise rebuild has yet to start
            self._rebuild_data["check"] = "not yet started"

        if verbose:
            self.log.info("%s query rebuild data: %s", str(self), self._rebuild_data)

    def _wait_for_rebuild(self, expected, interval=1):
        """Wait for the rebuild to start or end.

        Args:
            expected (str): which rebuild data check to wait for: 'running' or 'completed'
            interval (int): number of seconds to wait in between rebuild completion checks

        Raises:
            DaosTestError: if waiting for rebuild times out.

        """
        self.log.info(
            "Waiting for rebuild to be %s%s ...",
            expected,
            " with a {} second timeout".format(self.rebuild_timeout.value)
            if self.rebuild_timeout.value is not None else "")

        # If waiting for rebuild to start and it is detected as completed, stop waiting
        expected_set = set()
        expected_set.add(expected)
        expected_set.add("completed")

        start = time()
        self._update_rebuild_data()
        while self._rebuild_data["check"] not in expected_set:
            self.log.info("  Rebuild is %s ...", self._rebuild_data["check"])
            if self.rebuild_timeout.value is not None:
                if time() - start > self.rebuild_timeout.value:
                    raise DaosTestError(
                        "TIMEOUT detected after {} seconds while for waiting for rebuild to be {}. "
                        "This timeout can be adjusted via the 'pool/rebuild_timeout' test yaml "
                        "parameter.".format(self.rebuild_timeout.value, expected))
            sleep(interval)
            self._update_rebuild_data()

        self.log.info("Wait for rebuild complete: rebuild %s", self._rebuild_data["check"])

    def has_rebuild_started(self, verbose=True):
        """Determine if rebuild has started.

        Args:
            verbose (bool, optional): whether to display the pool query info. Defaults to True.

        Returns:
            bool: True if rebuild has started

        """
        self._update_rebuild_data(verbose)
        return self._rebuild_data["check"] == "running"

    def has_rebuild_completed(self, verbose=True):
        """Determine if rebuild has completed.

        Args:
            verbose (bool, optional): whether to display the pool query info. Defaults to True.

        Returns:
            bool: True if rebuild has completed

        """
        self._update_rebuild_data(verbose)
        return self._rebuild_data["check"] == "completed"

    def wait_for_rebuild_to_start(self, interval=1):
        """Wait for the rebuild to start.

        Args:
            interval (int): number of seconds to wait in between rebuild completion checks

        Raises:
            DaosTestError: if waiting for rebuild times out.

        """
        self._wait_for_rebuild("running", interval)

    def wait_for_rebuild_to_end(self, interval=1):
        """Wait for the rebuild to end.

        Args:
            interval (int): number of seconds to wait in between rebuild completion checks

        Raises:
            DaosTestError: if waiting for rebuild times out.

        """
        self._wait_for_rebuild("completed", interval)

    def measure_rebuild_time(self, operation, interval=1):
        """Measure rebuild time.

        This method is mainly for debugging purpose. We'll analyze the output when we
        realize that rebuild is taking too long.

        Args:
            operation (str): Type of operation to print in the log.
            interval (int): Interval (sec) to call pool query to check the rebuild status.
                Defaults to 1.
        """
        start = time()
        self.wait_for_rebuild_to_start(interval=interval)
        self.wait_for_rebuild_to_end(interval=interval)
        duration = time() - start
        self.log.info("%s duration: %.1f sec", operation, duration)

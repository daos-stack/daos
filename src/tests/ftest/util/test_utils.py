#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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
from logging import getLogger
import os
from time import sleep, time

from avocado import fail_on
from avocado.utils import process
from command_utils import BasicParameter, ObjectWithParameters
from pydaos.raw import (DaosApiError, DaosServer, DaosContainer, DaosPool,
                        c_uuid_to_str)
from general_utils import check_pool_files, get_random_string, DaosTestError
from env_modules import load_mpi


class CallbackHandler(object):
    """Defines a callback method to use with DaosApi class methods."""

    def __init__(self, delay=1):
        """Create a CallbackHandler object.

        Args:
            delay (int, optional): number of seconds to wait in between
                checking if the callback() method has been called.
                Defaults to 1.
        """
        self.delay = delay
        self.ret_code = None
        self.obj = None
        self._called = False
        self.log = getLogger(__name__)

    def callback(self, event):
        """Return an event from a DaosApi class method.

        Args:
            event (CallbackEvent): event returned by the DaosApi class method
        """
        # Get the return code and calling object from the event
        self.ret_code = event.event.ev_error
        self.obj = event.obj

        # Indicate that this method has being called
        self._called = True

    def wait(self):
        """Wait for this object's callback() method to be called."""
        # Reset the event return code and calling object
        self.ret_code = None
        self.obj = None

        # Wait for the callback() method to be called
        while not self._called:
            self.log.info(" Waiting ...")
            sleep(self.delay)

        # Reset the flag indicating that the callback() method was called
        self._called = False


class TestDaosApiBase(ObjectWithParameters):
    # pylint: disable=too-few-public-methods
    """A base class for functional testing of DaosPools objects."""

    def __init__(self, namespace, cb_handler=None):
        """Create a TestDaosApi object.

        Args:
            namespace (str): yaml namespace (path to parameters)
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
        """
        super(TestDaosApiBase, self).__init__(namespace)
        self.cb_handler = cb_handler
        self.log = getLogger(__name__)

    def _call_method(self, method, kwargs):
        """Call the DAOS API class method with the optional callback method.

        Args:
            method (object): method to call
            kwargs (dict): keyworded arguments for the method
        """
        if self.cb_handler:
            kwargs["cb_func"] = self.cb_handler.callback

        try:
            method(**kwargs)
        except DaosApiError as error:
            # Log the exception to obtain additional trace information
            self.log.debug(
                "Exception raised by %s.%s(%s)",
                method.__module__, method.__name__,
                ", ".join(
                    ["{}={}".format(key, val) for key, val in kwargs.items()]),
                exc_info=error)
            # Raise the exception so it can be handled by the caller
            raise error

        if self.cb_handler:
            # Wait for the call back if one is provided
            self.cb_handler.wait()

    def _check_info(self, check_list):
        """Verify each info attribute value matches an expected value.

        Args:
            check_list (list): a list of tuples containing the name of the
                information attribute to check, the current value of the
                attribute, and the expected value of the attribute. If the
                expected value is specified as a string with a number preceeded
                by '<', '<=', '>', or '>=' then this comparision will be used
                instead of the defult '=='.

        Returns:
            bool: True if at least one check has been specified and all the
            actual and expected values match; False otherwise.

        """
        check_status = len(check_list) > 0
        for check, actual, expect in check_list:
            # Determine which comparision to utilize for this check
            compare = ("==", lambda x, y: x == y, "does not match")
            if isinstance(expect, str):
                comparisions = {
                    "<": (lambda x, y: x < y, "is too large"),
                    ">": (lambda x, y: x > y, "is too small"),
                    "<=": (
                        lambda x, y: x <= y, "is too large or does not match"),
                    ">=": (
                        lambda x, y: x >= y, "is too small or does not match"),
                }
                for key, val in comparisions.items():
                    # If the expected value is preceeded by one of the known
                    # comparision keys, use the comparision and remove the key
                    # from the expected value
                    if expect[:len(key)] == key:
                        compare = (key, val[0], val[1])
                        expect = expect[len(key):]
                        try:
                            expect = int(expect)
                        except ValueError:
                            # Allow strings to be strings
                            pass
                        break
            self.log.info(
                "Verifying the %s %s: %s %s %s",
                self.__class__.__name__.replace("Test", "").lower(),
                check, actual, compare[0], expect)
            if not compare[1](actual, expect):
                msg = "  The {} {}: actual={}, expected={}".format(
                    check, compare[2], actual, expect)
                self.log.error(msg)
                check_status = False
        return check_status


class TestPool(TestDaosApiBase):
    """A class for functional testing of DaosPools objects."""

    def __init__(self, context, log=None, cb_handler=None):
        # pylint: disable=unused-argument
        """Initialize a TestPool object.

        Note: 'log' is now a defunct argument and will be removed in the future

        Args:
            context (DaosContext): [description]
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

        self.pool = None
        self.uuid = None
        self.info = None
        self.svc_ranks = None
        self.connected = False

    @fail_on(DaosApiError)
    def create(self):
        """Create a pool.

        Destroys an existing pool if defined and assigns self.pool and
        self.uuid.
        """
        self.destroy()
        if self.target_list.value is not None:
            self.log.info(
                "Creating a pool on targets %s", self.target_list.value)
        else:
            self.log.info("Creating a pool")
        self.pool = DaosPool(self.context)
        kwargs = {
            "mode": self.mode.value, "uid": self.uid, "gid": self.gid,
            "scm_size": self.scm_size.value, "group": self.name.value}
        for key in ("target_list", "svcn", "nvme_size"):
            value = getattr(self, key).value
            if value:
                kwargs[key] = value
        self._call_method(self.pool.create, kwargs)
        self.uuid = self.pool.get_uuid_str()
        self.svc_ranks = [
            int(self.pool.svc.rl_ranks[index])
            for index in range(self.pool.svc.rl_nr)]
        self.log.info(
            "  Pool created with uuid %s and svc ranks %s",
            self.uuid, self.svc_ranks)

    @fail_on(DaosApiError)
    def connect(self, permission=1):
        """Connect to the pool.

        Args:
            permission (int, optional): connect permission. Defaults to 1.

        Returns:
            bool: True if the pool has been connected; False if the pool was
                already connected or the pool is not defined.

        """
        if self.pool and not self.connected:
            kwargs = {"flags": 1 << permission}
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

    @fail_on(DaosApiError)
    def destroy(self, force=1):
        """Destroy the pool.

        Args:
            force (int, optional): force flag. Defaults to 1.

        Returns:
            bool: True if the pool has been destroyed; False if the pool is not
                defined.

        """
        if self.pool:
            self.disconnect()
            self.log.info("Destroying pool %s", self.uuid)
            if self.pool.attached:
                self._call_method(self.pool.destroy, {"force": force})
            self.pool = None
            self.uuid = None
            self.info = None
            self.svc_ranks = None
            return True
        return False

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


class TestContainerData(object):
    """A class for storing data written to DaosContainer objects."""

    def __init__(self, debug=False):
        """Create a TestContainerData object.

        Args:
            debug (bool, optional): if set log the write/read_record calls.
                Defaults to False.
        """
        self.obj = None
        self.txn = None
        self.records = []
        self.log = getLogger(__name__)
        self.debug = debug

    def get_akeys(self):
        """Get a list of all the akeys currently being used.

        Returns:
            list: a list of all the used akeys
        """
        return [record["akey"] for record in self.records]

    def get_dkeys(self):
        """Get a list of all the dkeys currently being used.

        Returns:
            list: a list of all the used dkeys

        """
        return [record["dkey"] for record in self.records]

    def _log_method(self, name, kwargs):
        """Log the method call with its arguments.

        Args:
            name (str): method name
            kwargs (dict): dictionary of method arguments
        """
        if self.debug:
            args = ", ".join(
                ["{}={}".format(key, kwargs[key]) for key in sorted(kwargs)])
            self.log.debug("  %s(%s)", name, args)

    def write_record(self, container, akey, dkey, data, rank=None,
                     obj_class=None):
        """Write a record to the container.

        Args:
            container (TestContainer): container in which to write the object
            akey (str): the akey
            dkey (str): the dkey
            data (object): the data to write as a string or list
            rank (int, optional): rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.

        Raises:
            DaosTestError: if there was an error writing the record
        """
        self.records.append({"akey": akey, "dkey": dkey, "data": data})
        kwargs = {"dkey": dkey, "akey": akey, "obj": self.obj, "rank": rank}
        if obj_class:
            kwargs["obj_cls"] = obj_class
        try:
            if isinstance(data, list):
                kwargs["datalist"] = data
                self._log_method("write_an_array_value", kwargs)
                (self.obj, self.txn) = \
                    container.container.write_an_array_value(**kwargs)
            else:
                kwargs["thedata"] = data
                kwargs["size"] = len(data)
                self._log_method("write_an_obj", kwargs)
                (self.obj, self.txn) = \
                    container.container.write_an_obj(**kwargs)
        except DaosApiError as error:
            raise DaosTestError(
                "Error writing {}data (dkey={}, akey={}, data={}) to "
                "container {}: {}".format(
                    "array " if isinstance(data, list) else "", dkey, akey,
                    data, container.uuid, error))

    def write_object(self, container, record_qty, akey_size, dkey_size,
                     data_size, rank=None, obj_class=None, data_array_size=0):
        """Write an object to the container.

        Args:
            container (TestContainer): container in which to write the object
            record_qty (int): the number of records to write
            rank (int, optional): rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.
            data_array_size (optional): write an array or single value.
                                        Defaults to 0.

        Raises:
            DaosTestError: if there was an error writing the object
        """
        for _ in range(record_qty):
            akey = get_random_string(akey_size, self.get_akeys())
            dkey = get_random_string(dkey_size, self.get_dkeys())
            if data_array_size == 0:
                data = get_random_string(data_size)
            else:
                data = [
                    get_random_string(data_size)
                    for _ in range(data_array_size)]
            # Write single data to the container
            self.write_record(container, akey, dkey, data, rank, obj_class)
            # Verify the data was written correctly
            data_read = self.read_record(
                container, akey, dkey, data_size, data_array_size)
            if data != data_read:
                raise DaosTestError(
                    "Written data confirmation failed:"
                    "\n  wrote: {}\n  read:  {}".format(data, data_read))

    def read_record(self, container, akey, dkey, data_size, data_array_size=0):
        """Read a record from the container.

        Args:
            container (TestContainer): container in which to write the object
            akey (str): the akey
            dkey (str): the dkey
            data_size (int): size of the data to read
            data_array_size (int): size of array item

        Raises:
            DaosTestError: if there was an error reading the object

        Returns:
            str: the data read for the container
        """
        kwargs = {"dkey": dkey, "akey": akey, "obj": self.obj, "txn": self.txn}
        try:
            if data_array_size > 0:
                kwargs["rec_count"] = data_array_size
                kwargs["rec_size"] = data_size + 1
                self._log_method("read_an_array", kwargs)
                read_data = container.container.read_an_array(**kwargs)
            else:
                kwargs["size"] = data_size
                self._log_method("read_an_obj", kwargs)
                read_data = container.container.read_an_obj(**kwargs)
        except DaosApiError as error:
            raise DaosTestError(
                "Error reading {}data (dkey={}, akey={}, size={}) from "
                "container {}: {}".format(
                    "array " if data_array_size > 0 else "", dkey, akey,
                    data_size, container.uuid, error))
        return [data[:-1] for data in read_data] \
            if data_array_size > 0 else read_data.value

    def read_object(self, container):
        """Read an object from the container.

        Args:
            container (TestContainer): container from which to read the object

        Returns:
            bool: True if ll the records where read successfully and matched
                what was originally written; False otherwise
        """
        status = len(self.records) > 0
        for record_info in self.records:
            expect = record_info["data"]
            kwargs = {
                "container": container,
                "akey": record_info["akey"],
                "dkey": record_info["dkey"],
                "data_size": len(expect[0].split()),
            }
            try:
                if isinstance(expect, list):
                    kwargs["data_size"] = len(expect[0]) if expect else 0
                    kwargs["data_array_size"] = len(expect)
                else:
                    kwargs["data_size"] = len(expect)
                    kwargs["data_array_size"] = 0
                actual = self.read_record(**kwargs)
            except DaosApiError as error:
                container.log.error(error)
                status = False
            finally:
                if actual != expect:
                    container.log.error(
                        "Error data mismatch: expected: %s, actual: %s",
                        expect, actual)
                    status = False
        return status


class TestContainer(TestDaosApiBase):
    """A class for functional testing of DaosContainer objects."""

    def __init__(self, pool, cb_handler=None):
        """Create a TeestContainer object.

        Args:
            pool (TestPool): the test pool in which to create the container
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
        """
        super(TestContainer, self).__init__("/run/container/*", cb_handler)
        self.pool = pool

        self.object_qty = BasicParameter(None)
        self.record_qty = BasicParameter(None)
        self.akey_size = BasicParameter(None)
        self.dkey_size = BasicParameter(None)
        self.data_size = BasicParameter(None)
        self.data_array_size = BasicParameter(0, 0)

        self.container = None
        self.uuid = None
        self.info = None
        self.opened = False
        self.written_data = []

    @fail_on(DaosApiError)
    def create(self, uuid=None):
        """Create a container.

        Args:
            uuid (str, optional): contianer uuid. Defaults to None.
        """
        self.destroy()
        self.log.info(
            "Creating a container with pool handle %s",
            self.pool.pool.handle.value)
        self.container = DaosContainer(self.pool.context)
        kwargs = {"poh": self.pool.pool.handle}
        if uuid is not None:
            kwargs["con_uuid"] = uuid
        self._call_method(self.container.create, kwargs)
        self.uuid = self.container.get_uuid_str()
        self.log.info("  Container created with uuid %s", self.uuid)

    @fail_on(DaosApiError)
    def open(self, pool_handle=None, container_uuid=None):
        """Open the container with pool handle and container UUID if provided.

        Args:
            pool_handle (TestPool.pool.handle, optional): Pool handle.
            Defaults to None.
                If you don't provide it, the default pool handle in
                DaosContainer will be used.
                If you created a TestPool instance and want to use its pool
                handle, pass in something like self.pool[-1].pool.handle.value
            container_uuid (hex, optional): Container UUID. Defaults to None.
                If you want to use certain container's UUID, pass in
                something like uuid.UUID(self.container[-1].uuid)

        Returns:
            bool: True if the container has been opened; False if the container
                is already opened.

        """
        if self.container and not self.opened:
            self.log.info("Opening container %s", self.uuid)
            kwargs = {}
            kwargs["poh"] = pool_handle
            kwargs["cuuid"] = container_uuid
            self._call_method(self.container.open, kwargs)
            self.opened = True
            return True
        return False

    @fail_on(DaosApiError)
    def close(self):
        """Close the container.

        Returns:
            bool: True if the container has been closed; False if the container
                is already closed.

        """
        if self.container and self.opened:
            self.log.info("Closing container %s", self.uuid)
            self._call_method(self.container.close, {})
            self.opened = False
            return True
        return False

    @fail_on(DaosApiError)
    def destroy(self, force=1):
        """Destroy the container.

        Args:
            force (int, optional): force flag. Defaults to 1.

        Returns:
            bool: True if the container has been destroyed; False if the
                container does not exist.

        """
        if self.container:
            self.close()
            self.log.info("Destroying container %s", self.uuid)
            if self.container.attached:
                self._call_method(self.container.destroy, {"force": force})
            self.container = None
            self.uuid = None
            self.info = None
            self.written_data = []
            return True
        return False

    @fail_on(DaosApiError)
    def get_info(self, coh=None):
        """Query the container for information.

        Sets the self.info attribute.

        Args:
            coh (str, optional): container handle override. Defaults to None.
        """
        if self.container:
            self.open()
            self.log.info("Querying container %s", self.uuid)
            self._call_method(self.container.query, {"coh": coh})
            self.info = self.container.info

    def check_container_info(self, ci_uuid=None, es_hce=None, es_lre=None,
                             es_lhe=None, es_ghce=None, es_glre=None,
                             es_ghpce=None, ci_nsnapshots=None,
                             ci_min_slipped_epoch=None):
        # pylint: disable=unused-argument
        """Check the container info attributes.

        Note:
            Arguments may also be provided as a string with a number preceeded
            by '<', '<=', '>', or '>=' for other comparisions besides the
            default '=='.

        Args:
            ci_uuid (str, optional): container uuid. Defaults to None.
            es_hce (int, optional): hc epoch?. Defaults to None.
            es_lre (int, optional): lr epoch?. Defaults to None.
            es_lhe (int, optional): lh epoch?. Defaults to None.
            es_ghce (int, optional): ghc epoch?. Defaults to None.
            es_glre (int, optional): glr epoch?. Defaults to None.
            es_ghpce (int, optional): ghpc epoch?. Defaults to None.
            ci_nsnapshots (int, optional): number of snapshots.
                Defaults to None.
            ci_min_slipped_epoch (int, optional): . Defaults to None.

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
             if key == "ci_uuid" else getattr(self.info, key),
             val)
            for key, val in locals().items()
            if key != "self" and val is not None]
        return self._check_info(checks)

    @fail_on(DaosTestError)
    def write_objects(self, rank=None, obj_class=None, debug=False):
        """Write objects to the container.

        Args:
            rank (int, optional): server rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.
            debug (bool, optional): log the record write/read method calls.
                Defaults to False.

        Raises:
            DaosTestError: if there was an error writing the object
        """
        self.open()
        self.log.info(
            "Writing %s object(s), with %s record(s) of %s bytes(s) each, in "
            "container %s%s%s",
            self.object_qty.value, self.record_qty.value, self.data_size.value,
            self.uuid, " on rank {}".format(rank) if rank is not None else "",
            " with object class {}".format(obj_class)
            if obj_class is not None else "")
        for _ in range(self.object_qty.value):
            self.written_data.append(TestContainerData(debug))
            self.written_data[-1].write_object(
                self, self.record_qty.value, self.akey_size.value,
                self.dkey_size.value, self.data_size.value, rank, obj_class,
                self.data_array_size.value)

    @fail_on(DaosTestError)
    def read_objects(self, debug=False):
        """Read the objects from the container and verify they match.

        Args:
            debug (bool, optional): log the record read method calls. Defaults
                to False.

        Returns:
            bool: True if
        """
        self.open()
        self.log.info(
            "Reading %s object(s) in container %s",
            len(self.written_data), self.uuid)
        status = len(self.written_data) > 0
        for data in self.written_data:
            data.debug = debug
            status &= data.read_object(self)
        return status

    def execute_io(self, duration, rank=None, obj_class=None, debug=False):
        """Execute writes and reads for the specified time period.

        Args:
            duration (int): how long, in seconds, to write and read data
            rank (int, optional): server rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.
            debug (bool, optional): log the record write/read method calls.
                Defaults to False.

        Returns:
            int: number of bytes written to the container

        Raises:
            DaosTestError: if there is an error writing, reading, or verify the
                data

        """
        self.open()
        self.log.info(
            "Writing and reading objects in container %s for %s seconds",
            self.uuid, duration)

        total_bytes_written = 0
        finish_time = time() + duration
        while time() < finish_time:
            self.written_data.append(TestContainerData(debug))
            self.written_data[-1].write_object(
                self, 1, self.akey_size.value, self.dkey_size.value,
                self.data_size.value, rank, obj_class)
            total_bytes_written += self.data_size.value
        return total_bytes_written

    def get_target_rank_lists(self, message=""):
        """Get a list of lists of target ranks from each written object.

        Args:
            message (str, optional): message to include in the target rank list
                output. Defaults to "".

        Raises:
            DaosTestError: if there is an error obtaining the target rank list
                from the DaosObj

        Returns:
            list: a list of list of targets for each written object in this
                container

        """
        self.open()
        target_rank_lists = []
        for data in self.written_data:
            try:
                data.obj.get_layout()
                # Convert the list of longs into a list of ints
                target_rank_lists.append(
                    [int(rank) for rank in data.obj.tgt_rank_list])
            except DaosApiError as error:
                raise DaosTestError(
                    "Error obtaining target rank list for object {} in "
                    "container {}: {}".format(data.obj, self.uuid, error))
        if message is not None:
            self.log.info("Target rank lists%s:", message)
            for ranks in target_rank_lists:
                self.log.info("  %s", ranks)
        return target_rank_lists

    def get_target_rank_count(self, rank, target_rank_list):
        """Get the number of times a rank appears in the target rank list.

        Args:
            rank (int): the rank to count. Defaults to None.
            target_rank_list (list): a list of lists of target ranks per object

        Returns:
            (int): the number of object rank lists containing the rank

        """
        count = sum([ranks.count(rank) for ranks in target_rank_list])
        self.log.info(
            "Occurrences of rank %s in the target rank list: %s", rank, count)
        return count

    def punch_objects(self, indices):
        """Punch committed objects from the container.

        Args:
            indices (list): list of index numbers indicating which written
                objects to punch from the self.written_data list

        Raises:
            DaosTestError: if there is an error punching the object or
                determining which objec to punch

        Returns:
            int: number of successfully punched objects

        """
        self.open()
        self.log.info(
            "Punching %s objects from container %s with %s written objects",
            len(indices), self.uuid, len(self.written_data))
        count = 0
        if len(self.written_data) > 0:
            for index in indices:
                # Find the object to punch at the specified index
                txn = 0
                try:
                    obj = self.written_data[index].obj
                except IndexError:
                    raise DaosTestError(
                        "Invalid index {} for written data".format(index))

                # Close the object
                self.log.info(
                    "Closing object (index: %s, txn: %s) in container %s",
                    index, txn, self.uuid)
                try:
                    self._call_method(obj.close, {})
                except DaosApiError as error:
                    self.log.error("  Error: %s", str(error))
                    continue

                # Punch the object
                self.log.info(
                    "Punching object (index: %s, txn: %s) from container %s",
                    index, txn, self.uuid)
                try:
                    self._call_method(obj.punch, {"txn": txn})
                    count += 1
                except DaosApiError as error:
                    self.log.error("  Error: %s", str(error))

        # Retutrn the number of punched objects
        return count

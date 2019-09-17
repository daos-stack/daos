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
from __future__ import print_function

import os
from time import sleep, time

from avocado import fail_on
from avocado.utils import process
from conversion import c_uuid_to_str
from command_utils import BasicParameter, ObjectWithParameters
from daos_api import DaosApiError, DaosServer, DaosContainer, DaosPool
from general_utils import check_pool_files, get_random_string, DaosTestError


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
            print(" Waiting ...")
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

    def _call_method(self, method, kwargs):
        """Call the DAOS API class method with the optional callback method.

        Args:
            method (object): method to call
            kwargs (dict): keyworded arguments for the method
        """
        if self.cb_handler:
            kwargs["cb_func"] = self.cb_handler.callback
        method(**kwargs)
        if self.cb_handler:
            # Wait for the call back if one is provided
            self.cb_handler.wait()


class TestPool(TestDaosApiBase):
    """A class for functional testing of DaosPools objects."""

    def __init__(self, context, log, cb_handler=None):
        """[summary].

        Args:
            context (DaosContext): [description]
            log (logging): logging object used to report the pool status
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
        """
        super(TestPool, self).__init__("/run/pool/*", cb_handler)
        self.context = context
        self.log = log
        self.uid = os.geteuid()
        self.gid = os.getegid()

        self.mode = BasicParameter(None)
        self.name = BasicParameter(None)
        self.group = BasicParameter(None)
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
        self.log.info(
            "Creating a pool{}".format(
                " on targets {}".format(self.target_list.value)
                if self.target_list.value else ""))
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
        self.log.info("  Pool created with uuid {} and svc ranks {}".format(
            self.uuid, self.svc_ranks))

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

    def check_rebuild_status(self, rs_version=None, rs_pad_32=None,
                             rs_errno=None, rs_done=None,
                             rs_toberb_obj_nr=None, rs_obj_nr=None,
                             rs_rec_nr=None):
        # pylint: disable=unused-argument
        """Check the pool info rebuild attributes.

        Args:
            rs_version (int, optional): rebuild version. Defaults to None.
            rs_pad_32 (int, optional): rebuild pad. Defaults to None.
            rs_errno (int, optional): rebuild error number. Defaults to None.
            rs_done (int, optional): rebuild done flag. Defaults to None.
            rs_toberb_obj_nr (int, optional): number of objects to be rebuilt.
                Defaults to None.
            rs_obj_nr (int, optional): number of rebuilt objects.
                Defaults to None.
            rs_rec_nr (int, optional): number of rebuilt records.
                Defaults to None.

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

    def _check_info(self, check_list):
        """Verify each pool info attribute value matches an expected value.

        Args:
            check_list (list): a list of tuples containing the name of the pool
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
                "Verifying the pool %s: %s %s %s",
                check, actual, compare[0], expect)
            if not compare[1](actual, expect):
                msg = "  The {} {}: actual={}, expected={}".format(
                    check, compare[2], actual, expect)
                self.log.error(msg)
                check_status = False
        return check_status

    def rebuild_complete(self):
        """Determine if the pool rebuild is complete.

        Returns:
            bool: True if pool rebuild is complete; False otherwise

        """
        self.get_info()
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
    def start_rebuild(self, server_group, rank, daos_log):
        """Kill a specific server rank using this pool.

        Args:
            server_group (str): daos server group name
            rank (int): daos server rank to kill
            daos_log (DaosLog): object for logging messages

        Returns:
            bool: True if the server has been killed and the rank has been
            excluded from the pool; False if the pool is undefined

        """
        msg = "Killing DAOS server {} (rank {})".format(server_group, rank)
        self.log.info(msg)
        daos_log.info(msg)
        server = DaosServer(self.context, server_group, rank)
        server.kill(1)
        return self.exclude(rank, daos_log)

    @fail_on(DaosApiError)
    def exclude(self, rank, daos_log):
        """Manually exclude a rank from this pool.

        Args:
            rank (int): daos server rank to kill
            daos_log (DaosLog): object for logging messages

        Returns:
            bool: True if rank has been excluded from the pool; False if the
                pool is undefined

        """
        if self.pool:
            msg = "Excluding server rank {} from pool {}".format(
                rank, self.uuid)
            self.log.info(msg)
            daos_log.info(msg)
            self.pool.exclude([rank])
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
        self.log.info("Writing {} bytes to pool {}".format(size, self.uuid))
        env = {
            "DAOS_POOL": self.uuid,
            "DAOS_SVCL": "1",
            "DAOS_SINGLETON_CLI": "1",
            "PYTHONPATH": os.getenv("PYTHONPATH", ""),
        }
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


class TestContainerData(object):
    """A class for storing data written to DaosContainer objects."""

    def __init__(self):
        """Create a TestContainerData object."""
        self.obj = None
        self.txn = None
        self.records = []

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

    def write_record(self, container, akey, dkey, data, rank=None,
                     obj_class=None):
        """Write a record to the container.

        Args:
            container (TestContainer): container in which to write the object
            akey (str): the akey
            dkey (str): the dkey
            data (str): the data to write
            rank (int, optional): rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.

        Raises:
            DaosTestError: if there was an error writing the record

        """
        self.records.append({"akey": akey, "dkey": dkey, "data": data})
        try:
            kwargs = {
                "thedata": data, "size": len(data), "dkey": dkey, "akey": akey,
                "obj": self.obj, "rank": rank}
            if obj_class:
                kwargs["obj_cls"] = obj_class
            (self.obj, self.txn) = container.container.write_an_obj(**kwargs)
        except DaosApiError as error:
            raise DaosTestError(
                "Error writing data (dkey={}, akey={}, data={}) to "
                "container {}: {}".format(
                    dkey, akey, data, container.uuid, error))

    def write_object(self, container, record_qty, akey_size, dkey_size,
                     data_size, rank=None, obj_class=None):
        """Write an object to the container.

        Args:
            container (TestContainer): container in which to write the object
            record_qty (int): [description]
            rank (int, optional): [description]. Defaults to None.
            obj_class (int, optional): [description]. Defaults to None.

        Raises:
            DaosTestError: if there was an error writing the object

        """
        for _ in range(record_qty):
            akey = get_random_string(akey_size, self.get_akeys())
            dkey = get_random_string(dkey_size, self.get_dkeys())
            data = get_random_string(data_size)

            # Write single data to the container
            self.write_record(container, akey, dkey, data, rank, obj_class)

            # Verify the data was written correctly
            data_read = self.read_record(container, akey, dkey, data_size)
            if data != data_read:
                raise DaosTestError(
                    "Written data confirmation failed:"
                    "\n  wrote: {}\n  read:  {}".format(data, data_read))

    def read_record(self, container, akey, dkey, data_size):
        """Read a record from the container.

        Args:
            container (TestContainer): container in which to write the object
            akey (str): the akey
            dkey (str): the dkey
            data_size (int): size of the data to read

        Raises:
            DaosTestError: if there was an error reading the object

        Returns:
            str: the data read for the container

        """
        try:
            read_data = container.container.read_an_obj(
                data_size, dkey, akey, self.obj, self.txn)
        except DaosApiError as error:
            raise DaosTestError(
                "Error reading data (dkey={}, akey={}, size={}) from "
                "container {}: {}".format(
                    dkey, akey, data_size, container.uuid, error))
        return read_data.value

    def read_object(self, container):
        """Read an object from the container.

        Args:
            container (TestContainer): container in which to write the object

        Returns:
            bool: True if ll the records where read successfully and matched
                what was originally written; False otherwise

        """
        status = len(self.records) > 0
        for record_info in self.records:
            akey = record_info["akey"]
            dkey = record_info["dkey"]
            expect = record_info["data"]
            try:
                actual = self.read_record(container, akey, dkey, len(expect))
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
        self.log = self.pool.log

        self.object_qty = BasicParameter(None)
        self.record_qty = BasicParameter(None)
        self.akey_size = BasicParameter(None)
        self.dkey_size = BasicParameter(None)
        self.data_size = BasicParameter(None)

        self.container = None
        self.uuid = None
        self.opened = False
        self.written_data = []

    @fail_on(DaosApiError)
    def create(self, uuid=None):
        """Create a container.

        Args:
            uuid (str, optional): contianer uuid. Defaults to None.
        """
        self.destroy()
        print("***pool_uuid***:{}".format(self.pool.pool.get_uuid_str()))
        self.log.info(
            "Creating a container with pool handle %s",
            self.pool.pool.handle.value)
        self.container = DaosContainer(self.pool.context)
        kwargs = {"poh": self.pool.pool.handle}
        if uuid is not None:
            kwargs["con_uuid"] = uuid
        self._call_method(self.container.create, kwargs)
        self.uuid = self.container.get_uuid_str()
        self.log.info("  Container created with uuid {}".format(self.uuid))

    @fail_on(DaosApiError)
    def open(self):
        """Open the container.

        Returns:
            bool: True if the container has been opened; False if the container
                is already opened.

        """
        if self.container and not self.opened:
            self.log.info("Opening container %s", self.uuid)
            self._call_method(self.container.open, {})
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
            self._call_method(self.container.destroy, {"force": force})
            self.container = None
            self.written_data = []
            return True
        return False

    @fail_on(DaosTestError)
    def write_objects(self, rank=None, obj_class=None):
        """Write objects to the container.

        Args:
            rank (int, optional): server rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.

        Raises:
            DaosTestError: if there was an error writing the object

        """
        self.open()
        self.log.info(
            "Writing objects in container %s%s%s", self.uuid,
            " on rank {}".format(
                rank) if not isinstance(rank, type(None)) else "",
            " with object class {}".format(obj_class) if obj_class else "")
        for _ in range(self.object_qty.value):
            self.written_data.append(TestContainerData())
            self.written_data[-1].write_object(
                self, self.record_qty.value, self.akey_size.value,
                self.dkey_size.value, self.data_size.value, rank, obj_class)

    @fail_on(DaosTestError)
    def read_objects(self):
        """Read the objects from the container and verify they match.

        Returns:
            bool: True if

        """
        self.open()
        self.log.info("Reading objects in container %s", self.uuid)
        status = len(self.written_data) > 0
        for data in self.written_data:
            status &= data.read_object(self)
        return status

    def execute_io(self, duration, rank=None, obj_class=None):
        """Execute writes and reads for the specified time period.

        Args:
            duration (int): how long, in seconds, to write and read data
            rank (int, optional): server rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.

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
            self.written_data.append(TestContainerData())
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
            "Occurrences of rank {} in the target rank list: {}".format(
                rank, count))
        return count

#!/usr/bin/python
'''
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
'''
from __future__ import print_function

import os
import re
import json
import random
import string
from pathlib import Path
from errno import ENOENT
from time import sleep

from avocado import fail_on
from avocado.utils import process
from conversion import c_uuid_to_str
from daos_api import DaosApiError, DaosServer, DaosContainer, DaosPool
from ClusterShell.Task import task_self
from ClusterShell.NodeSet import NodeSet


class DaosTestError(Exception):
    """DAOS API exception class."""


def get_file_path(bin_name, dir_path=""):
    """
    Find the binary path name in daos_m and return the list of path.

    args:
        bin_name: bin file to be.
        dir_path: Directory location on top of daos_m to find the
                  bin.
    return:
        list: list of the paths for bin_name file
    Raises:
        OSError: If failed to find the bin_name file
    """
    with open('../../../.build_vars.json') as json_file:
        build_paths = json.load(json_file)
    basepath = os.path.normpath(build_paths['PREFIX'] + "/../{0}"
                                .format(dir_path))

    file_path = list(Path(basepath).glob('**/{0}'.format(bin_name)))
    if not file_path:
        raise OSError(ENOENT, "File {0} not found inside {1} Directory"
                      .format(bin_name, basepath))
    else:
        return file_path


def process_host_list(hoststr):
    """
    Convert a slurm style host string into a list of individual hosts.

    e.g. boro-[26-27] becomes a list with entries boro-26, boro-27

    This works for every thing that has come up so far but I don't know what
    all slurmfinds acceptable so it might not parse everything possible.
    """
    # 1st split into cluster name and range of hosts
    split_loc = hoststr.index('-')
    cluster = hoststr[0:split_loc]
    num_range = hoststr[split_loc+1:]

    # if its just a single host then nothing to do
    if num_range.isdigit():
        return [hoststr]

    # more than 1 host, remove the brackets
    host_list = []
    num_range = re.sub(r'\[|\]', '', num_range)

    # differentiate between ranges and single numbers
    hosts_and_ranges = num_range.split(',')
    for item in hosts_and_ranges:
        if item.isdigit():
            hostname = cluster + '-' + item
            host_list.append(hostname)
        else:
            # split the two ends of the range
            host_range = item.split('-')
            for hostnum in range(int(host_range[0]), int(host_range[1])+1):
                hostname = "{}-{}".format(cluster, hostnum)
                host_list.append(hostname)

    return host_list


def get_random_string(length, exclude=None):
    """Create a specified length string of random ascii letters and numbers.

    Optionally exclude specific random strings from being returned.

    Args:
        length (int): length of the string to return
        exclude (list|None): list of strings to not return

    Returns:
        str: a string of random ascii letters and numbers

    """
    exclude = exclude if isinstance(exclude, list) else []
    random_string = None
    while not isinstance(random_string, str) or random_string in exclude:
        random_string = "".join(
            random.choice(string.ascii_uppercase + string.digits)
            for _ in range(length))
    return random_string


@fail_on(DaosApiError)
def get_pool(context, mode, size, name, svcn=1, log=None, connect=True):
    """Return a DAOS pool that has been created an connected.

    Args:
        context (DaosContext): the context to use to create the pool
        mode (int): the pool mode
        size (int): the size of the pool
        name (str): the name of the pool
        svcn (int): the pool service leader quantity
        log (DaosLog, optional): object for logging messages. Defaults to None.
        connect (bool, optional): connect to the new pool. Defaults to True.

    Returns:
        DaosPool: an object representing a DAOS pool

    """
    if log:
        log.info("Creating a pool")
    pool = DaosPool(context)
    pool.create(mode, os.geteuid(), os.getegid(), size, name, svcn=svcn)
    if connect:
        if log:
            log.info("Connecting to the pool")
        pool.connect(1 << 1)
    return pool


@fail_on(DaosApiError)
def get_container(context, pool, log=None):
    """Retrun a DAOS a container that has been created an opened.

    Args:
        context (DaosContext): the context to use to create the container
        pool (DaosPool): pool in which to create the container
        log (DaosLog|None): object for logging messages

    Returns:
        DaosContainer: an object representing a DAOS container

    """
    if log:
        log.info("Creating a container")
    container = DaosContainer(context)
    container.create(pool.handle)
    if log:
        log.info("Opening a container")
    container.open()
    return container


@fail_on(DaosApiError)
def kill_server(server_group, context, rank, pool, log=None):
    """Kill a specific server rank.

    Args:
        server_group (str): daos server group name
        context (DaosContext): the context to use to create the DaosServer
        rank (int): daos server rank to kill
        pool (DaosPool): the DaosPool from which to exclude the rank
        log (DaosLog|None): object for logging messages

    Returns:
        None

    """
    if log:
        log.info("Killing DAOS server {} (rank {})".format(server_group, rank))
    server = DaosServer(context, server_group, rank)
    server.kill(1)
    if log:
        log.info("Excluding server rank {}".format(rank))
    pool.exclude([rank])


@fail_on(DaosApiError)
def get_pool_status(pool, log):
    """Determine if the pool rebuild is complete.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status

    Returns:
        PoolInfo: the PoolInfo object returned by the pool's pool_query()
            function

    """
    pool_info = pool.pool_query()
    message = "Pool: pi_ntargets={}".format(pool_info.pi_ntargets)
    message += ", pi_nnodes={}".format(
        pool_info.pi_nnodes)
    message += ", pi_ndisabled={}".format(
        pool_info.pi_ndisabled)
    message += ", rs_version={}".format(
        pool_info.pi_rebuild_st.rs_version)
    message += ", rs_done={}".format(
        pool_info.pi_rebuild_st.rs_done)
    message += ", rs_toberb_obj_nr={}".format(
        pool_info.pi_rebuild_st.rs_toberb_obj_nr)
    message += ", rs_obj_nr={}".format(
        pool_info.pi_rebuild_st.rs_obj_nr)
    message += ", rs_rec_nr={}".format(
        pool_info.pi_rebuild_st.rs_rec_nr)
    message += ", rs_errno={}".format(
        pool_info.pi_rebuild_st.rs_errno)
    log.info(message)
    return pool_info


def is_pool_rebuild_complete(pool, log):
    """Determine if the pool rebuild is complete.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status

    Returns:
        bool: pool rebuild completion status

    """
    get_pool_status(pool, log)
    return pool.pool_info.pi_rebuild_st.rs_done == 1


def wait_for_rebuild(pool, log, to_start, interval):
    """Wait for the rebuild to start or end.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status
        to_start (bool): whether to wait for rebuild to start or end
        interval (int): number of seconds to wait in between rebuild
            completion checks

    Returns:
        None

    """
    log.info(
        "Waiting for rebuild to %s ...",
        "start" if to_start else "complete")
    while is_pool_rebuild_complete(pool, log) == to_start:
        log.info(
            "  Rebuild %s ...",
            "has not yet started" if to_start else "in progress")
        sleep(interval)


def verify_rebuild(pool, log, to_be_rebuilt, object_qty, record_qty, errors=0):
    """Confirm the number of rebuilt objects reported by the pool query.

    Args:
        pool (DaosPool): pool for which to determine if rebuild is complete
        log (logging): logging object used to report the pool status
        to_be_rebuilt (int): expected number of objects to be rebuilt
        object_qty (int): expected number of rebuilt records
        record_qty (int): expected total number of rebuilt records
        errors (int): expected number of rebuild errors

    Returns:
        list: a list of error messages for each expected value that did not
            match the actual value.  If all expected values were detected the
            list will be empty.

    """
    messages = []
    expected_pool_info = {
        "rs_toberb_obj_nr": to_be_rebuilt,
        "rs_obj_nr": object_qty,
        "rs_rec_nr": record_qty,
        "rs_errno": errors
    }
    log.info("Verifying the number of rebuilt objects and status")
    pool_info = get_pool_status(pool, log)
    for key, expected in expected_pool_info.items():
        detected = getattr(pool_info.pi_rebuild_st, key)
        if detected != expected:
            messages.append(
                "Unexpected {} value: expected={}, detected={}".format(
                    key, expected, detected))
    return messages


def check_pool_files(log, hosts, uuid):
    """Check if pool files exist on the specified list of hosts.

    Args:
        log (logging): logging object used to display messages
        hosts (list): list of hosts
        uuid (str): uuid file name to look for in /mnt/daos.

    Returns:
        bool: True if the files for this pool exist on each host; False
            otherwise

    """
    file_list = (uuid, "superblock")
    expect = len(file_list) * len(hosts)
    actual = 0
    nodeset = NodeSet.fromlist(hosts)
    task = task_self()

    log.info("Checking for pool data on %s", nodeset)
    for fname in file_list:
        task.run(
            "test -e /mnt/daos/{}; echo $?".format(fname), nodes=nodeset)
        for output, node_list in task.iter_buffers():
            if output == "0":
                actual += len(node_list)
            else:
                nodes = NodeSet.fromlist(node_list)
                log.error("%s: /mnt/daos/%s not found", nodes, fname)
    return expect == actual


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


class TestParameter(object):
    # pylint: disable=too-few-public-methods
    """A class for test parameters whose values are read from a yaml file."""

    def __init__(self, value, default=None):
        """Create a TestParameter object.

        Args:
            value (object): intial value for the parameter
            default (object, optional): default value. Defaults to None.
        """
        self.value = value if value is not None else default
        self.default = default

    def __str__(self):
        """Convert this object into a string.

        Returns:
            str: the string version of the parameter's value

        """
        return str(self.value)

    def set_value(self, name, test, path):
        """Set the value for the paramter from the test case's yaml file.

        Args:
            name (str): name of the value in the yaml file
            test (Test): avocado Test object to use to read the yaml file
            path (str): yaml path where the name is to be found
        """
        self.value = test.params.get(name, path, self.default)


class TestDaosApiBase(object):
    # pylint: disable=too-few-public-methods
    """A base class for functional testing of DaosPools objects."""

    def __init__(self, cb_handler=None):
        """Create a TestDaosApi object.

        Args:
            cb_handler (CallbackHandler, optional): callback object to use with
                the API methods. Defaults to None.
        """
        self.cb_handler = cb_handler

    def get_params(self, test, path):
        """Get the pool parameters from the yaml file.

        Args:
            test (Test): avocado Test object
            path (str): yaml namespace
        """
        for name, test_param in self.__dict__.items():
            if isinstance(test_param, TestParameter):
                test_param.set_value(name, test, path)

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
        super(TestPool, self).__init__(cb_handler)
        self.context = context
        self.log = log
        self.uid = os.geteuid()
        self.gid = os.getegid()

        self.mode = TestParameter(None)
        self.name = TestParameter(None)
        self.group = TestParameter(None)
        self.svcn = TestParameter(None)
        self.target_list = TestParameter(None)
        self.scm_size = TestParameter(None)
        self.nvme_size = TestParameter(None)

        self.pool = None
        self.uuid = None
        self.info = None
        self.connected = False

    def get_params(self, test, path="/run/pool/*"):
        """Get the pool parameters from the yaml file.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to "/run/pool/*".
        """
        super(TestPool, self).get_params(test, path)

    @fail_on(DaosApiError)
    def create(self):
        """Create a pool.

        Destroys an existing pool if defined and assigns self.pool and
        self.uuid.
        """
        self.destroy()
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
            bool: True if the pool has been destoyed; False if the pool is not
                defined.

        """
        if self.pool:
            self.disconnect()
            self.log.info("Destroying pool %s", self.uuid)
            self._call_method(self.pool.destroy, {"force": force})
            self.pool = None
            self.uuid = None
            self.info = None
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
                attribute, and the expected value of the attribute.

        Returns:
            bool: True if at least one check has been specified and all the
            actual and expected values match; False otherwise.

        """
        check_status = len(check_list) > 0
        for check, actual, expect in check_list:
            self.log.info(
                "Verifying the pool %s: %s ?= %s", check, actual, expect)
            if actual != expect:
                msg = "The {} does not match: actual: {}, expected: {}".format(
                    check, actual, expect)
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
        """
        msg = "Killing DAOS server {} (rank {})".format(server_group, rank)
        self.log.info(msg)
        daos_log.info(msg)
        server = DaosServer(self.context, server_group, rank)
        server.kill(1)
        msg = "Excluding server rank {} from pool {}".format(rank, self.uuid)
        self.log.info(msg)
        daos_log.info(msg)
        self.pool.exclude([rank])

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
        }
        current_path = os.path.dirname(os.path.abspath(__file__))
        command = "{} --np {} --hostfile {} {} {} testfile".format(
            orterun, processes, hostfile,
            os.path.join(current_path, "write_some_data.py"), size)
        return process.run(command, timeout, True, False, "both", True, env)


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
        super(TestContainer, self).__init__(cb_handler)
        self.pool = pool
        self.log = self.pool.log

        self.object_qty = TestParameter(None)
        self.record_qty = TestParameter(None)
        self.akey_size = TestParameter(None)
        self.dkey_size = TestParameter(None)
        self.data_size = TestParameter(None)

        self.container = None
        self.uuid = None
        self.opened = False
        self.written_data = []

    def get_params(self, test, path="/run/container/*"):
        """Get the container parameters from the yaml file.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace. Defaults to
                "/run/container/*".
        """
        super(TestContainer, self).get_params(test, path)

    @fail_on(DaosApiError)
    def create(self, uuid=None):
        """Create a container.

        Args:
            uuid (str, optional): contianer uuid. Defaults to None.
        """
        self.destroy()
        self.log.info("Creating a container")
        self.container = DaosContainer(self.pool.context)
        self.container.create(self.pool.pool.handle, uuid)
        self.uuid = self.container.get_uuid_str()

    @fail_on(DaosApiError)
    def open(self):
        """Open the container.

        Returns:
            bool: True if the container has been opened; False if the container
                is already opened.

        """
        if not self.opened:
            self.log.info("Opening container %s", self.uuid)
            self.container.open()
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
        if self.opened:
            self.log.info("Closing container %s", self.uuid)
            self.container.close()
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
            self.container.destroy(force)
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
        """
        self.open()
        self.log.info("Writing objects in container %s", self.uuid)
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

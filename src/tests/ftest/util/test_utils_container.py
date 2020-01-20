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
from logging import getLogger
from time import time

from test_utils_base import TestDaosApiBase

from avocado import fail_on
from command_utils import BasicParameter
from pydaos.raw import (DaosApiError, DaosContainer, c_uuid_to_str)
from general_utils import get_random_string, DaosTestError

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

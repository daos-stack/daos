"""
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
# pylint: disable=too-many-lines

import ctypes
from logging import getLogger
from time import time

from avocado import fail_on
from pydaos.raw import (DaosApiError, DaosContainer, DaosInputParams,
                        c_uuid_to_str, str_to_c_uuid)

from test_utils_base import TestDaosApiBase
from command_utils_base import BasicParameter
from exception_utils import CommandFailure
from general_utils import get_random_bytes, DaosTestError


class TestContainerData():
    """A class for storing data written to DaosContainer objects."""

    def __init__(self, debug=False):
        """Create a TestContainerData object.

        Args:
            debug (bool, optional): if set log the write/read_record calls.
                Defaults to False.
        """
        self.obj = None
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
            akey (bytes): the akey
            dkey (bytes): the dkey
            data (object): the data to write as a list bytes
            rank (int, optional): rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.

        Raises:
            DaosTestError: if there was an error writing the record

        """
        self.records.append(
            {"akey": akey, "dkey": dkey, "data": data, "punched": False})
        kwargs = {"dkey": dkey, "akey": akey, "obj": self.obj, "rank": rank}
        if obj_class:
            kwargs["obj_cls"] = obj_class
        try:
            if isinstance(data, list):
                kwargs["datalist"] = data
                self._log_method("write_an_array_value", kwargs)
                (self.obj) = \
                    container.container.write_an_array_value(**kwargs)
            else:
                kwargs["thedata"] = data
                kwargs["size"] = len(data)
                self._log_method("write_an_obj", kwargs)
                (self.obj) = \
                    container.container.write_an_obj(**kwargs)
        except DaosApiError as error:
            raise DaosTestError(
                "Error writing {}data (dkey={}, akey={}, data={}) to "
                "container {}: {}".format(
                    "array " if isinstance(data, list) else "", dkey, akey,
                    data, container.uuid, error)) from error

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
            akey = get_random_bytes(akey_size, self.get_akeys())
            dkey = get_random_bytes(dkey_size, self.get_dkeys())
            if data_array_size == 0:
                data = get_random_bytes(data_size)
            else:
                data = [
                    get_random_bytes(data_size)
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

    def read_record(self, container, akey, dkey, data_size, data_array_size=0,
                    txn=None):
        """Read a record from the container.

        Args:
            container (TestContainer): container in which to write the object
            akey (bytes): the akey
            dkey (bytes): the dkey
            data_size (int): size of the data to read
            data_array_size (int): size of array item
            txn (int, optional): transaction timestamp to read. Defaults to None
                which uses the last timestamp written.

        Raises:
            DaosTestError: if there was an error reading the object

        Returns:
            str: the data read for the container

        """
        kwargs = {
            "dkey": dkey,
            "akey": akey,
            "obj": self.obj,
            "txn": txn,
        }
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
                    data_size, container.uuid, error)) from error
        return [data[:-1] for data in read_data] \
            if data_array_size > 0 else read_data.value

    def read_object(self, container, txn=None):
        """Read an object from the container.

        Args:
            container (TestContainer): container from which to read the object
            txn (int, optional): transaction timestamp to read. Defaults to None
                which uses the last timestamp written.

        Returns:
            bool: True if all the records where read successfully and matched
                what was originally written; False otherwise

        """
        status = len(self.records) > 0
        for record_info in self.records:
            data = record_info["data"]
            kwargs = {
                "container": container,
                "akey": record_info["akey"],
                "dkey": record_info["dkey"],
                "txn": txn,
            }
            try:
                if isinstance(data, list):
                    kwargs["data_size"] = len(data[0]) if data else 0
                    kwargs["data_array_size"] = len(data)
                else:
                    kwargs["data_size"] = len(data)
                    kwargs["data_array_size"] = 0
                actual = self.read_record(**kwargs)

            except DaosApiError as error:
                self.log.error(
                    "    %s",
                    str(error).replace(
                        ") from",
                        ", punched={}) from".format(record_info["punched"])))
                status = False
                continue

            expect = b"" if record_info["punched"] else record_info["data"]
            if actual != expect:
                self.log.error(
                    "    Error data mismatch (akey=%s, dkey=%s, punched=%s): "
                    "expected: %s, actual: %s",
                    record_info["akey"], record_info["dkey"],
                    record_info["punched"], expect, actual)
                status = False
        return status


class TestContainer(TestDaosApiBase):  # pylint: disable=too-many-public-methods
    """A class for functional testing of DaosContainer objects."""

    def __init__(self, pool, daos_command=None, label_generator=None):
        """Create a TestContainer object.

        Args:
            pool (TestPool): the test pool in which to create the container
            daos_command (DaosCommand, optional): daos command object. Defaults to None
            label_generator (LabelGenerator, optional): used to generate container label by adding
                a number to self.label. Defaults to None

        """
        super().__init__("/run/container/*")
        self.pool = pool

        self.object_qty = BasicParameter(None)
        self.record_qty = BasicParameter(None)
        self.akey_size = BasicParameter(None)
        self.dkey_size = BasicParameter(None)
        self.data_size = BasicParameter(None)
        self.data_array_size = BasicParameter(0, 0)
        # Provider access to get input params values
        # for enabling different container properties
        self.input_params = DaosInputParams()

        # Optional daos command object to use with the USE_DAOS control method
        self.daos = daos_command

        # Optional daos command argument values to use with the USE_DAOS control
        # method when creating/destroying containers
        self.path = BasicParameter(None)
        self.type = BasicParameter(None)
        self.oclass = BasicParameter(None)
        self.dir_oclass = BasicParameter(None)
        self.file_oclass = BasicParameter(None)
        self.chunk_size = BasicParameter(None)
        self.properties = BasicParameter(None)
        self.daos_timeout = BasicParameter(None)
        self.label = BasicParameter(None, "TestContainer")
        self.label_generator = label_generator

        self.container = None
        self.uuid = None
        self.info = None
        self.opened = False
        self.written_data = []
        self.epoch = None

        # If defined, use container labels for most operations by default.
        # Setting to False will use the UUID where possible.
        self.use_label = True

    def __str__(self):
        """Return a string representation of this TestContainer object.

        Returns:
            str: 'label (uuid)' if using labels, else 'uuid'

        """
        if self.container is not None:
            if self.label.value:
                return "{} ({})".format(self.label.value, self.uuid)
            return str(self.uuid)
        return super().__str__()

    @property
    def identifier(self):
        """Get the container uuid or label.

        Returns:
            str: label if using labels and one is defined; otherwise the uuid

        """
        if self.use_label and self.label.value is not None:
            return self.label.value
        return self.uuid

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        Sets each BasicParameter object's value to the yaml key that matches
        the assigned name of the BasicParameter object in this class. For
        example, the self.block_size.value will be set to the value in the yaml
        file with the key 'block_size'.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        super().get_params(test)
        if self.daos:
            self.daos.timeout = self.daos_timeout.value

        # Use a unique container label if a generator is supplied
        if self.label.value and self.label_generator:
            self.label.update(self.label_generator.get_label(self.label.value))

    @fail_on(DaosApiError)
    @fail_on(CommandFailure)
    def create(self, con_in=None, acl_file=None):
        """Create a container.

        Args:
            con_in (optional): to be defined. Defaults to None.
            acl_file (str, optional): path of the ACL file. Defaults to None.

        Returns:
            dict: the daos json command output converted to a python dictionary
            None: if control_method is API

        """
        self.destroy()
        if not self.silent.value:
            self.log.info(
                "Creating a container with pool handle %s",
                self.pool.pool.handle.value)
        self.container = DaosContainer(self.pool.context)
        result = None

        if self.control_method.value == self.USE_API:
            # pydaos.raw doesn't support create with a label
            self.log.info("Ignoring label for container created with API")
            self.label.update(None)

            # Create a container with the API method
            kwargs = {"poh": self.pool.pool.handle}

            # Refer daos_api for setting input params for DaosContainer.
            cop = self.input_params.get_con_create_params()
            if con_in is not None:
                cop.type = con_in[0]
                cop.enable_chksum = con_in[1]
                cop.srv_verify = con_in[2]
                cop.chksum_type = con_in[3]
                cop.chunk_size = con_in[4]
                cop.rd_lvl = con_in[5]
            else:
                # Default to RANK fault domain (rd_lvl:1) when not specified
                cop.rd_lvl = ctypes.c_uint64(1)

            kwargs["con_prop"] = cop

            self._call_method(self.container.create, kwargs)

        else:
            # Disconnect the pool if connected
            self.pool.disconnect()

            # Create a container with the daos command
            kwargs = {
                "pool": self.pool.identifier,
                "sys_name": self.pool.name.value,
                "path": self.path.value,
                "cont_type": self.type.value,
                "oclass": self.oclass.value,
                "dir_oclass": self.dir_oclass.value,
                "file_oclass": self.file_oclass.value,
                "chunk_size": self.chunk_size.value,
                "properties": self.properties.value,
                "acl_file": acl_file,
                "label": self.label.value
            }

            self._log_method("daos.container_create", kwargs)
            try:
                result = self.daos.container_create(**kwargs)
                if result["status"] != 0:
                    # The command failed but no exception was raised, so let the caller handle
                    return result
                uuid = result["response"]["container_uuid"]
            except KeyError as error:
                raise CommandFailure("Error: Unexpected daos container create output") from error
            # Populate the empty DaosContainer object with the properties of the
            # container created with daos container create.
            self.container.uuid = str_to_c_uuid(uuid)
            self.container.attached = 1
            self.container.poh = self.pool.pool.handle

        self.uuid = self.container.get_uuid_str()
        if not self.silent.value:
            self.log.info("  Created container %s", str(self))

        return result

    @fail_on(CommandFailure)
    def create_snap(self, *args, **kwargs):
        """Create container snapshot by calling daos container create-snap.

        Sets self.epoch to the created epoch.

        Args:
            args (tuple, optional): positional arguments to DaosCommand.container_create_snap
            kwargs (dict, optional): named arguments to DaosCommand.container_create_snap

        Returns:
            str: JSON output of daos container create-snap

        Raises:
            CommandFailure: Raised from the daos command call

        """
        result = self.daos.container_create_snap(
            pool=self.pool.identifier, cont=self.identifier, *args, **kwargs)
        if result["status"] == 0:
            self.epoch = result["response"]["epoch"]
        return result

    @fail_on(CommandFailure)
    def destroy_snap(self, *args, **kwargs):
        """Destroy container snapshot by calling daos container destroy-snap.

        Sets self.epoch to the created epoch.

        Args:
            args (tuple, optional): positional arguments to DaosCommand.container_destroy_snap
            kwargs (dict, optional): named arguments to DaosCommand.container_destroy_snap

        Returns:
            str: JSON output of daos container destroy-snap

        Raises:
            CommandFailure: Raised from the daos command call

        """
        result = self.daos.container_destroy_snap(
            pool=self.pool.identifier, cont=self.identifier, *args, **kwargs)
        if result["status"] == 0:
            self.epoch = None
        return result

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
                is already opened or not created.

        """
        if self.container and not self.opened:
            self.log.info("Opening container %s", str(self))
            self.pool.connect()
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
                is already closed or not created.

        """
        if self.container and self.opened:
            self.log.info("Closing container %s", str(self))
            self._call_method(self.container.close, {})
            self.opened = False
            return True
        return False

    @fail_on(DaosApiError)
    @fail_on(CommandFailure)
    def destroy(self, force=1):
        """Destroy the container.

        Args:
            force (int, optional): force flag. Defaults to 1.

        Returns:
            bool: True if the container has been destroyed; False if the
                container does not exist.

        """
        status = False
        if self.container:
            self.close()
            if not self.silent.value:
                self.log.info("Destroying container %s", str(self))
            if self.container.attached:
                kwargs = {"force": force}

                if self.control_method.value == self.USE_API:
                    # Destroy the container with the API method
                    self._call_method(self.container.destroy, kwargs)
                    status = True

                else:
                    # Disconnect the pool if connected
                    self.pool.disconnect()

                    # Destroy the container with the daos command
                    kwargs["pool"] = self.pool.identifier
                    kwargs["sys_name"] = self.pool.name.value
                    kwargs["cont"] = self.identifier
                    self._log_method("daos.container_destroy", kwargs)
                    self.daos.container_destroy(**kwargs)
                    status = True

            self.container = None
            self.uuid = None
            self.info = None
            self.written_data = []

        return status

    @fail_on(DaosApiError)
    def get_info(self, coh=None):
        """Query the container for information.

        Sets the self.info attribute.

        Args:
            coh (str, optional): container handle override. Defaults to None.

        """
        if self.container:
            self.open()
            self.log.info("Querying container %s", str(self))
            self._call_method(self.container.query, {"coh": coh})
            self.info = self.container.info

    def check_container_info(self, ci_uuid=None, ci_nsnapshots=None, ci_nhandles=None):
        # pylint: disable=unused-argument
        """Check the container info attributes.

        Note:
            Arguments may also be provided as a string with a number preceded
            by '<', '<=', '>', or '>=' for other comparisons besides the
            default '=='.

        Args:
            ci_uuid (str, optional): container uuid. Defaults to None.
            ci_nsnapshots (int, optional): number of snapshots.
                Defaults to None.

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
             if key == "ci_uuid" else getattr(self.info, key),
             val)
            for key, val in list(locals().items())
            if key != "self" and val is not None]
        return self._check_info(checks)

    def write_objects_wo_failon(self, rank=None, obj_class=None):
        """Write objects to the container without fail_on DaosTestError,
           for negative test on container write_objects.

        Args:
            rank (int, optional): server rank. Defaults to None.
            obj_class (int, optional): daos object class. Defaults to None.

        """
        self.open()
        self.log.info(
            "Writing %s object(s), with %s record(s) of %s bytes(s) each, in "
            "container %s%s%s",
            self.object_qty.value, self.record_qty.value, self.data_size.value,
            str(self), " on rank {}".format(rank) if rank is not None else "",
            " with object class {}".format(obj_class)
            if obj_class is not None else "")
        for _ in range(self.object_qty.value):
            self.written_data.append(TestContainerData(self.debug.value))
            self.written_data[-1].write_object(
                self, self.record_qty.value, self.akey_size.value,
                self.dkey_size.value, self.data_size.value, rank, obj_class,
                self.data_array_size.value)

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
            "Writing %s object(s), with %s record(s) of %s bytes(s) each, in "
            "container %s%s%s",
            self.object_qty.value, self.record_qty.value, self.data_size.value,
            str(self), " on rank {}".format(rank) if rank is not None else "",
            " with object class {}".format(obj_class)
            if obj_class is not None else "")
        for _ in range(self.object_qty.value):
            self.written_data.append(TestContainerData(self.debug.value))
            self.written_data[-1].write_object(
                self, self.record_qty.value, self.akey_size.value,
                self.dkey_size.value, self.data_size.value, rank, obj_class,
                self.data_array_size.value)

    @fail_on(DaosTestError)
    def read_objects(self, txn=None):
        """Read the objects from the container and verify they match.

        Args:
            txn (int, optional): transaction timestamp to read. Defaults to None
                which uses the last timestamp written.

        Returns:
            bool: True if all the container objects contain the same previously
                written records; False otherwise

        """
        self.open()
        self.log.info(
            "Reading %s object(s) in container %s",
            len(self.written_data), str(self))
        status = len(self.written_data) > 0
        for data in self.written_data:
            data.debug = self.debug.value
            status &= data.read_object(self, txn)
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
            DaosTestError: if there is an error writing, reading, or verify the data

        """
        self.open()
        self.log.info(
            "Writing and reading objects in container %s for %s seconds",
            str(self), duration)

        total_bytes_written = 0
        finish_time = time() + duration
        while time() < finish_time:
            self.written_data.append(TestContainerData(self.debug.value))
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
                # Convert the list of longs into a list of integers
                target_rank_lists.append(
                    [int(rank) for rank in data.obj.tgt_rank_list])
            except DaosApiError as error:
                raise DaosTestError(
                    "Error obtaining target rank list for object {} in "
                    "container {}: {}".format(
                        data.obj, str(self), error)) from error
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
        count = sum(ranks.count(rank) for ranks in target_rank_list)
        self.log.info("Occurrences of rank %s in the target rank list: %s", rank, count)
        return count

    def punch_objects(self, indices):
        """Punch committed objects from the container.

        Args:
            indices (list): list of index numbers indicating which written
                objects to punch from the self.written_data list

        Raises:
            DaosTestError: if there is an error punching the object or
                determining which object to punch

        Returns:
            int: number of successfully punched objects

        """
        self.open()
        self.log.info(
            "Punching %s objects from container %s with %s written objects",
            len(indices), str(self), len(self.written_data))
        count = 0
        if self.written_data:
            for index in indices:
                # Find the object to punch at the specified index
                txn = 0
                try:
                    obj = self.written_data[index].obj
                except IndexError as error:
                    raise DaosTestError(
                        "Invalid index {} for written data".format(
                            index)) from error

                # Close the object
                self.log.info(
                    "Closing object %s (index: %s, txn: %s) in container %s",
                    obj, index, txn, str(self))
                try:
                    self._call_method(obj.close, {})
                except DaosApiError:
                    continue

                # Punch the object
                self.log.info(
                    "Punching object %s (index: %s, txn: %s) from container %s",
                    obj, index, txn, str(self))
                try:
                    self._call_method(obj.punch, {"txn": txn})
                    count += 1
                except DaosApiError:
                    continue

                # Mark the object's records as punched
                for record in self.written_data[index].records:
                    record["punched"] = True

        # Return the number of punched objects
        return count

    def punch_records(self, indices, punch_dkey=True):
        """Punch committed records from the container.

        Args:
            indices (list): list of index numbers indicating which written
                records in the object to punch from the self.written_data list
            punch_dkey (bool, optional): whether to punch dkeys or akeys.
                Defaults to True (punch dkeys).

        Raises:
            DaosTestError: if there is an error punching the record or
                determining which record to punch

        Returns:
            int: number of successfully punched records

        """
        self.open()
        self.log.info(
            "Punching %s records from each object in container %s with %s "
            "written objects",
            len(indices), str(self), len(self.written_data))
        count = 0
        for data in self.written_data:
            # Close the object
            self.log.info(
                "Closing object %s in container %s",
                data.obj, str(self))
            try:
                self._call_method(data.obj.close, {})
            except DaosApiError:
                continue

            # Find the record to punch at the specified index
            for index in indices:
                try:
                    rec = data.records[index]
                except IndexError as error:
                    raise DaosTestError(
                        "Invalid record index {} for object {}".format(
                            index, data.obj)) from error

                # Punch the record
                self.log.info(
                    "Punching record %s (index: %s, akey: %s, dkey: %s) from "
                    "object %s in container %s",
                    rec, index, rec["akey"], rec["dkey"], data.obj, str(self))
                kwargs = {"txn": 0}
                try:
                    if punch_dkey:
                        kwargs["dkeys"] = [rec["dkey"]]
                        self._call_method(data.obj.punch_dkeys, kwargs)
                    else:
                        kwargs["dkey"] = rec["dkey"]
                        kwargs["akeys"] = [rec["akey"]]
                        self._call_method(data.obj.punch_akeys, kwargs)
                    count += 1
                except DaosApiError:
                    continue

                # Mark the record as punched
                rec["punched"] = True

        # Return the number of punched objects
        return count

    @fail_on(CommandFailure)
    def check(self, *args, **kwargs):
        """Check object consistency by calling daos container check.

        Args:
            args (tuple, optional): positional arguments to DaosCommand.container_check
            kwargs (dict, optional): named arguments to DaosCommand.container_check

        Returns:
            str: JSON output of daos container check.

        Raises:
            CommandFailure: Raised from the daos command call.

        """
        return self.daos.container_check(
            pool=self.pool.identifier, cont=self.identifier, *args, **kwargs)

    @fail_on(CommandFailure)
    def set_prop(self, *args, **kwargs):
        """Set container properties by calling daos container set-prop.

        Args:
            args (tuple, optional): positional arguments to DaosCommand.container_set_prop
            kwargs (dict, optional): named arguments to DaosCommand.container_set_prop

        Returns:
            str: JSON output of daos container set-prop.

        Raises:
            CommandFailure: Raised from the daos command call.

        """
        return self.daos.container_set_prop(
            pool=self.pool.identifier, cont=self.identifier, *args, **kwargs)

    @fail_on(CommandFailure)
    def get_prop(self, *args, **kwargs):
        """Get container properties by calling daos container get-prop.

        Args:
            args (tuple, optional): positional arguments to DaosCommand.container_get_prop
            kwargs (dict, optional): named arguments to DaosCommand.container_get_prop

        Returns:
            str: JSON output of daos container get-prop

        Raises:
            CommandFailure: Raised from the daos command call

        """
        return self.daos.container_get_prop(
            pool=self.pool.identifier, cont=self.identifier, *args, **kwargs)

    def get_prop_values(self, *args, **kwargs):
        """Get container property values by calling daos container get-prop.

        Args:
            args (tuple, optional): positional arguments to DaosCommand.container_get_prop
            kwargs (dict, optional): named arguments to DaosCommand.container_get_prop

        Returns:
            list: a list of values matching the or specified property names.

        """
        values = []
        self.log.info("Getting property values for container %s", self)
        data = self.get_prop(*args, **kwargs)
        if data['status'] != 0:
            return values
        for entry in data['response']:
            values.append(entry['value'])
        return values

    def verify_prop(self, expected_props):
        """Verify daos container get-prop returns expected values.

        Args:
            expected_props (dict): expected properties and values

        Returns:
            bool: whether props from daos container get-prop match expected values

        """
        prop_output = self.get_prop(properties=expected_props.keys())
        for actual_prop in prop_output['response']:
            if expected_props[actual_prop['name']] != actual_prop['value']:
                return False
        return True

    @fail_on(CommandFailure)
    def list_snaps(self):
        """Get container properties by calling daos container list-snaps.

        Returns:
            str: JSON output of daos container list-snaps

        Raises:
            CommandFailure: Raised from the daos command call

        """
        return self.daos.container_list_snaps(pool=self.pool.identifier, cont=self.identifier)

    @fail_on(CommandFailure)
    def query(self, *args, **kwargs):
        """Call daos container query.

        Args:
            args (tuple, optional): args to pass to container_query
            kwargs (dict, optional): keyword args to pass to container_query

        Returns:
            str: JSON output of daos container query.

        Raises:
            CommandFailure: Raised from the daos command call.

        """
        return self.daos.container_query(
            pool=self.pool.identifier, cont=self.identifier, *args, **kwargs)

    @fail_on(CommandFailure)
    def update_acl(self, entry=None, acl_file=None):
        """Update container acl by calling daos container update-acl.

        Args:
            entry (bool, optional): Add or modify a single ACL entry
            acl_file (str, optional): Input file containing ACL

        Returns:
            str: JSON output of daos container update-acl.

        Raises:
            CommandFailure: Raised from the daos command call.

        """
        return self.daos.container_update_acl(
            pool=self.pool.identifier, cont=self.identifier, entry=entry, acl_file=acl_file)

    def set_attr(self, *args, **kwargs):
        """Call daos container set-attr.

        Args:
            args (tuple, optional): positional arguments to DaosCommand.container_set_attr
            kwargs (dict, optional): named arguments to DaosCommand.container_set_attr

        Returns:
            CmdResult: Object that contains exit status, stdout, and other information.

        """
        return self.daos.container_set_attr(
            pool=self.pool.identifier, cont=self.identifier, *args, **kwargs)

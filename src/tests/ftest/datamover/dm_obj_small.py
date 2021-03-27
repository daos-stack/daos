#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from pydaos.raw import IORequest, DaosObj, DaosApiError
import ctypes
import avocado


class DmObjSmallTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Object Data Mover validation for syncing/cloning generic containers
       at the object level.

    Test Class Description:
        Tests the following cases:
            Cloning a small container with dcp.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        self.obj_list = []

        # Get the dataset parameters
        self.num_objs = self.params.get(
            "num_objs", "/run/dataset/*")
        self.num_dkeys_per_obj = self.params.get(
            "num_dkeys_per_obj", "/run/dataset/*")
        self.num_akeys_array_per_dkey = self.params.get(
            "num_akeys_array_per_dkey", "/run/dataset/*")
        self.num_akeys_single_per_dkey = self.params.get(
            "num_akeys_single_per_dkey", "/run/dataset/*")
        self.akey_sizes = self.params.get(
            "akey_sizes", "/run/dataset/*")
        self.akey_extents = self.params.get(
            "akey_extents", "/run/dataset/*")

    def create_dataset(self, cont):
        """Create the dataset.

        Args:
            cont (TestContainer): the container

        """
        self.log.info("Creating dataset in %s/%s",
            str(cont.pool.uuid), str(cont.uuid))

        cont.open()

        for obj_idx in range(self.num_objs):
            # Create a new obj
            obj = DaosObj(cont.pool.context, cont.container)
            self.obj_list.append(obj)

            obj.create(rank=obj_idx, objcls=2)
            obj.open()
            ioreq = IORequest(cont.pool.context, cont.container, obj)
            for dkey_idx in range(self.num_dkeys_per_obj):
                c_dkey = ctypes.create_string_buffer(
                    "dkey {}".format(dkey_idx).encode())

                for akey_idx in range(self.num_akeys_single_per_dkey):
                    # Round-robin to get the size of data and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(self.akey_sizes)
                    data_size = self.akey_sizes[akey_size_idx]
                    data_val = str(akey_idx % 10)
                    data = data_size * data_val
                    c_akey = ctypes.create_string_buffer(
                        "akey single {}".format(akey_idx).encode())
                    c_value = ctypes.create_string_buffer(data.encode())
                    c_size = ctypes.c_size_t(ctypes.sizeof(c_value))
                    ioreq.single_insert(c_dkey, c_akey, c_value, c_size)

                for akey_idx in range(self.num_akeys_array_per_dkey):
                    # Round-robin to get the size of data and
                    # the number of extents, and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(self.akey_sizes)
                    data_size = self.akey_sizes[akey_size_idx]
                    akey_extent_idx = akey_idx % len(self.akey_extents)
                    num_extents = self.akey_extents[akey_extent_idx]
                    c_data = []
                    akey = "akey array {}".format(akey_idx)
                    c_akey = ctypes.create_string_buffer(akey.encode())
                    for data_idx in range(num_extents):
                        data_val = str(data_idx % 10)
                        data = data_size * data_val
                        c_data.append([
                            ctypes.create_string_buffer(data.encode()),
                            data_size])
                    ioreq.insert_array(c_dkey, c_akey, c_data)
            obj.close()
        cont.close()

    def verify_dataset(self, cont):
        """Verify the dataset.

        Args:
            cont (TestContainer): the container

        """
        self.log.info("Verifying dataset in %s/%s",
            str(cont.pool.uuid), str(cont.uuid))

        cont.open()

        for obj_idx in range(self.num_objs):
            obj = DaosObj(cont.pool.context, cont.container,
                          self.obj_list[obj_idx].c_oid)
            obj.open()
            ioreq = IORequest(cont.pool.context, cont.container, obj)
            for dkey_idx in range(self.num_dkeys_per_obj):
                dkey = "dkey {}".format(dkey_idx)
                c_dkey = ctypes.create_string_buffer(dkey.encode())

                for akey_idx in range(self.num_akeys_single_per_dkey):
                    # Round-robin to get the size of data and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(self.akey_sizes)
                    data_size = self.akey_sizes[akey_size_idx]
                    data_val = str(akey_idx % 10)
                    data = str(data_size * data_val)
                    akey = "akey single {}".format(akey_idx)
                    c_akey = ctypes.create_string_buffer(akey.encode())
                    buf = ioreq.single_fetch(c_dkey, c_akey, data_size + 1)
                    actual_data = str(buf.value.decode())
                    if actual_data != data:
                        self.log.info("Expected:\n%s\nBut got:\n%s",
                            data[:100] + "...",
                            actual_data[:100] + "...")
                        self.log.info(
                            "For:\nobj: %s.%s\ndkey: %s\nakey: %s",
                                str(obj.c_oid.hi), str(obj.c_oid.lo),
                                dkey, akey)
                        self.fail("Single value verification failed.")

                for akey_idx in range(self.num_akeys_array_per_dkey):
                    # Round-robin to get the size of data and
                    # the number of extents, and
                    # arbitrarily use a number 0-9 to fill data
                    akey_size_idx = akey_idx % len(self.akey_sizes)
                    data_size = self.akey_sizes[akey_size_idx]
                    c_data_size = ctypes.c_size_t(data_size)
                    akey_extent_idx = akey_idx % len(self.akey_extents)
                    num_extents = self.akey_extents[akey_extent_idx]
                    c_num_extents = ctypes.c_uint(num_extents)
                    akey = "akey array {}".format(akey_idx)
                    c_akey = ctypes.create_string_buffer(akey.encode())
                    actual_data = ioreq.fetch_array(c_dkey, c_akey,
                                                    c_num_extents, c_data_size)
                    for data_idx in range(num_extents):
                        data_val = str(data_idx % 10)
                        data = str(data_size * data_val)
                        actual_idx = str(actual_data[data_idx].decode())
                        if data != actual_idx:
                            self.log.info("Expected:\n%s\nBut got:\n%s",
                                data[:100] + "...",
                                actual_idx + "...")
                            self.log.info(
                                "For:\nobj: %s.%s\ndkey: %s\nakey: %s",
                                    str(obj.c_oid.hi), str(obj.c_oid.lo),
                                    dkey, akey)
                            self.fail("Array verification failed.")
            obj.close()
        cont.close()

    def run_dm_obj_small(self, tool):
        """
        Test Description:
            Tests cloning a small container.
        Use Cases:
            Create pool1.
            Create cont1 in pool1.
            Create a small dataset in cont1.
            Clone cont1 to a new cont2 in pool1.
            Create pool2.
            Clone cont1 to a new cont3 in pool2.

        Args:
            tool (str): the tool to use. Must be in DataMoverTestBase.TOOLS
        """
        # Set the tool to use
        self.set_tool(tool)

        # Create pool1
        pool1 = self.create_pool()
        pool1.connect(2)

        # Create cont1
        cont1 = self.create_cont(pool1)

        # Create dataset in cont1
        self.create_dataset(cont1)

        # Generate a uuid for cont2
        cont2_uuid = self.gen_uuid()

        # Clone cont1 to a new cont2 in pool1
        self.run_datamover(
            self.test_id + " (cont1->cont2) (same pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool1, cont2_uuid)

        # Verify data in cont2
        cont2 = self.get_cont(pool1, cont2_uuid)
        self.verify_dataset(cont2)

        # Create pool2
        pool2 = self.create_pool()
        pool2.connect(2)

        # Generate a uuid for cont3
        cont3_uuid = self.gen_uuid()

        # Clone cont1 to a new cont3 in pool2
        self.run_datamover(
            self.test_id + " (cont1->cont3) (different pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool2, cont3_uuid)

        # Verify data in cont3
        cont3 = self.get_cont(pool2, cont3_uuid)
        self.verify_dataset(cont3)

        # Must destroy before closing pools
        cont1.destroy()
        cont2.destroy()
        cont3.destroy()
        pool1.disconnect()
        pool2.disconnect()

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_dcp(self):
        """
        Test Description:
            DAOS-6858: Verify cloning a small container.
        :avocado: tags=all,weekly_regression
        :avocado: tags=datamover,dcp
        :avocado: tags=dm_obj_small,dm_obj_small_dcp
        """
        self.run_dm_obj_small("DCP")

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_cont_clone(self):
        """
        Test Description:
            DAOS-6858: Verify cloning a small container.
        :avocado: tags=all,daily_regression
        :avocado: tags=datamover,cont_clone
        :avocado: tags=dm_obj_small,dm_obj_small_cont_clone
        """
        self.run_dm_obj_small("CONT_CLONE")

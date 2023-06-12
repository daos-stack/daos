'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from pydaos.raw import DaosApiError
import avocado

from data_mover_test_base import DataMoverTestBase


class DmvrSerialSmall(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Object Data Mover validation for serializing/deserializing
       generic containers at the object level.

    Test Class Description:
        Tests the following cases:
            Serializing a small container with daos-serialize.
            Deserializing a small container with daos-deserialize.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the dataset parameters
        self.num_objs = self.params.get(
            "num_objs", "/run/dataset/*")
        self.num_dkeys = self.params.get(
            "num_dkeys", "/run/dataset/*")
        self.num_akeys_single = self.params.get(
            "num_akeys_single", "/run/dataset/*")
        self.num_akeys_array = self.params.get(
            "num_akeys_array", "/run/dataset/*")
        self.akey_sizes = self.params.get(
            "akey_sizes", "/run/dataset/*")
        self.akey_extents = self.params.get(
            "akey_extents", "/run/dataset/*")

    def run_dm_serial_small(self, tool):
        """
        Test Description:
            Tests serializing/deserializing a small container.
        Use Cases:
            Create pool1.
            Create cont1 in pool1.
            Create a small dataset in cont1.
            Serialize cont1 to an HDF5 file(s).
            Create pool2.
            Deserialize the HDF5 file(s) to a new cont2 in pool2.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Create pool1
        pool1 = self.create_pool()
        pool1.connect(2)

        # Create cont1
        cont1 = self.get_container(pool1)

        # Create dataset in cont1
        obj_list = self.dataset_gen(
            cont1,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents)

        # Create pool2
        pool2 = self.create_pool()
        pool2.connect(2)

        # Serialize/Deserialize cont1 to a new cont2 in pool2
        result = self.run_datamover(
            self.test_id + " (cont1->HDF5->cont2)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool2, None)

        # Get the destination cont2 uuid
        cont2_label = self.parse_create_cont_label(result.stdout_text)

        # Verify data in cont2
        cont2 = self.get_cont(pool2, cont2_label)
        self.dataset_verify(
            obj_list, cont2,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents)

        # Must destroy before closing pools
        cont1.destroy()
        cont2.destroy()
        pool1.disconnect()
        pool2.disconnect()

    @avocado.fail_on(DaosApiError)
    def test_dm_serial_small_dserialize(self):
        """
        Test Description:
            DAOS-6875: Verify serializing a small container.
        :avocado: tags=all,pr
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_serialize,mfu_deserialize,hdf5
        :avocado: tags=DmvrSerialSmall,test_dm_serial_small_dserialize
        """
        self.run_dm_serial_small("DSERIAL")

'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from dfuse_utils import get_dfuse, start_dfuse
from duns_utils import format_path


# pylint: disable=too-many-ancestors
class DmvrSerialLargePosix(DataMoverTestBase):
    """Object Data Mover validation for serializing/deserializing
       POSIX containers at the object level.

    Test class Description:
        Tests the following cases:
            Serializing a large POSIX container with daos-serialize.
            Deserializing a large POSIX container with daos-deserialize.
    :avocado: recursive
    """

    def run_dm_serial_large_posix(self, tool):
        """
        Test Description:
            Tests serializing/deserializing a large POSIX container.
        Use Cases:
            Create pool1.
            Create cont1 in pool1.
            Create a large directory in cont1 with mdtest.
            Serialize cont1 to an HDF5 file(s).
            Create pool2.
            Deserialize the HDF5 file(s) to a new cont2 in pool2.

        Args:
            tool (str): the tool to use. Must be in DataMoverTestBase.TOOLS
        """
        # Set the tool to use
        self.set_tool(tool)

        # Get the mdtest params
        mdtest_flags = self.params.get("mdtest_flags", "/run/mdtest/*")
        file_size = self.params.get("bytes", "/run/mdtest/*")

        # Create pool1 and cont1
        pool1 = self.create_pool()
        cont1 = self.get_container(pool1)

        # Create a large directory in cont1
        self.mdtest_cmd.write_bytes.update(file_size)
        self.run_mdtest_with_params("DAOS", "/", pool1, cont1, flags=mdtest_flags[0])

        # Create pool2
        pool2 = self.create_pool()

        # Use dfuse as a shared intermediate for serialize + deserialize
        dfuse_cont = self.get_container(pool1)
        dfuse = get_dfuse(self, self.dfuse_hosts)
        start_dfuse(self, dfuse, pool1, dfuse_cont)
        self.serial_tmp_dir = dfuse.mount_dir.value

        # Serialize/Deserialize cont1 to a new cont2 in pool2
        result = self.run_datamover(
            self.test_id + " (cont1->HDF5->cont2)",
            src_path=format_path(pool1, cont1),
            dst_pool=pool2)

        # Get the destination cont2 uuid
        cont2_label = self.parse_create_cont_label(result.stdout_text)

        # Update mdtest params, read back and verify data from cont2
        self.mdtest_cmd.read_bytes.update(file_size)
        self.run_mdtest_with_params("DAOS", "/", pool2, cont2_label, flags=mdtest_flags[1])

    def test_dm_serial_large_posix_dserialize(self):
        """
        Test Description:
            DAOS-7432: Verify serializing a large POSIX container.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=datamover,mfu,mfu_serialize,mfu_deserialize,dfuse,dfs,mdtest,hdf5
        :avocado: tags=DmvrSerialLargePosix,test_dm_serial_large_posix_dserialize
        """
        self.run_dm_serial_large_posix("DSERIAL")

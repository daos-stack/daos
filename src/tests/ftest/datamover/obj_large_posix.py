'''
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase
from duns_utils import format_path


# pylint: disable=too-many-ancestors
class DmvrObjLargePosix(DataMoverTestBase):
    """Test class Description:
       Verify cloning a large POSIX container at the object level (non-DFS).

    :avocado: recursive
    """

    def run_dm_obj_large_posix(self, tool):
        """
        Test Description:
            Clone a large POSIX container to another POSIX container.
        Use Cases:
            Create a pool.
            Create POSIX type cont1.
            Create a large directory in cont1 with mdtest.
            Clone cont1 to a new cont2.
            Verify the directory in cont2 with mdtest.

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

        # Clone cont1 to cont2
        result = self.run_datamover(
            self.test_id + " (cont1 to cont2)",
            src_path=format_path(pool1, cont1),
            dst_path=format_path(pool1))
        cont2_label = self.parse_create_cont_label(result.stdout_text)

        # Update mdtest params, read back and verify data from cont2
        self.mdtest_cmd.read_bytes.update(file_size)
        self.run_mdtest_with_params("DAOS", "/", pool1, cont2_label, flags=mdtest_flags[1])

    def test_dm_obj_large_posix_dcp(self):
        """Jira ID: DAOS-6892

        Test Description:
            Clone a large POSIX container to another POSIX container using dcp.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=datamover,mfu,mfu_dcp,mdtest
        :avocado: tags=DmvrObjLargePosix,test_dm_obj_large_posix_dcp
        """
        self.run_dm_obj_large_posix("DCP")

    def test_dm_obj_large_posix_cont_clone(self):
        """Jira ID: DAOS-6892

        Test Description:
            Clone a large POSIX container to another POSIX container using
            daos cont clone.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,medium
        :avocado: tags=datamover,daos_cont_clone,mdtest
        :avocado: tags=DmvrObjLargePosix,test_dm_obj_large_posix_cont_clone
        """
        self.run_dm_obj_large_posix("CONT_CLONE")

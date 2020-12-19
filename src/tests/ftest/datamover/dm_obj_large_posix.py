#!/usr/bin/python
'''
  (C) Copyright 2020-2021 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from data_mover_test_base import DataMoverTestBase


# pylint: disable=too-many-ancestors
class DmObjLargePosix(DataMoverTestBase):
    """Test class Description:
       Verify cloning a large POSIX container at the object level (non-DFS)
       using dcp.

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
        """
        # Set the tool to use
        self.set_tool(tool)

        # Get the mdtest params
        mdtest_flags = self.params.get("mdtest_flags", "/run/mdtest/*")
        file_size = self.params.get("bytes", "/run/mdtest/*")

        # Create pool1 and cont1
        pool1 = self.create_pool()
        cont1 = self.create_cont(pool1)

        # Create a large directory in cont1
        self.mdtest_cmd.write_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", "/", pool1, cont1,
            flags=mdtest_flags[0])

        # Generate a uuid for cont2
        cont2_uuid = self.gen_uuid()

        # Clone cont1 to cont2
        self.run_datamover(
            self.test_id + " (cont1 to cont2)",
            "DAOS", None, pool1, cont1,
            "DAOS", None, pool1, cont2_uuid)

        # Update mdtest params, read back and verify data from cont2
        self.mdtest_cmd.read_bytes.update(file_size)
        self.run_mdtest_with_params(
            "DAOS", "/", pool1, cont2_uuid,
            flags=mdtest_flags[1])

    def test_dm_obj_large_posix_dcp(self):
        """Jira ID: DAOS-6892
        Test Description:
            Clone a large POSIX container to another POSIX container using dcp.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=datamover,dcp
        :avocado: tags=dm_obj_large_posix,dm_obj_large_posix_dcp
        """
        self.run_dm_obj_large_posix("DCP")

    def test_dm_obj_large_posix_cont_clone(self):
        """Jira ID: DAOS-6892
        Test Description:
            Clone a large POSIX container to another POSIX container using
            daos cont clone.
        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=datamover,cont_clone
        :avocado: tags=dm_obj_large_posix,dm_obj_large_posix_cont_clone
        """
        self.run_dm_obj_large_posix("CONT_CLONE")

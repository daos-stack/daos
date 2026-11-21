'''
  (C) Copyright 2020-2024 Intel Corporation.
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import avocado
from data_mover_test_base import DataMoverTestBase
from pydaos.raw import DaosApiError
from test_utils_container import get_existing_container


class DmvrObjSmallTest(DataMoverTestBase):
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

        # Get the dataset parameters
        self.num_objs = self.params.get(
            "num_objs", "/run/dataset/*")
        self.num_dkeys = self.params.get(
            "num_dkeys", "/run/dataset/*")
        self.num_akeys_array = self.params.get(
            "num_akeys_array", "/run/dataset/*")
        self.num_akeys_single = self.params.get(
            "num_akeys_single", "/run/dataset/*")
        self.akey_sizes = self.params.get(
            "akey_sizes", "/run/dataset/*")
        self.akey_extents = self.params.get(
            "akey_extents", "/run/dataset/*")
        self.punch_extents = self.params.get(
            "punch_extents", "/run/dataset/*", 0)
        self.punch_tail_extents = self.params.get(
            "punch_tail_extents", "/run/dataset/*", 0)

    def _gen_dataset(self, cont):
        """Create the dataset and confirm the source reads back as expected.

        Args:
            cont (TestContainer): the container to create the dataset in.

        Returns:
            list: a list of DaosObj created.

        """
        obj_list = self.dataset_gen(
            cont,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents,
            punch_extents=self.punch_extents,
            punch_tail_extents=self.punch_tail_extents)

        # the source itself must report the punched records as holes, otherwise
        # the runs below would pass without ever exercising the hole handling
        self._verify_dataset(obj_list, cont)

        return obj_list

    def _verify_dataset(self, obj_list, cont):
        """Verify a dataset created by _gen_dataset.

        Args:
            obj_list (list): obj_list returned from _gen_dataset.
            cont (TestContainer): the container to verify.
        """
        self.dataset_verify(
            obj_list, cont,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents,
            punch_extents=self.punch_extents,
            punch_tail_extents=self.punch_tail_extents)

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
        pool1 = self.get_pool()

        # Create cont1
        cont1 = self.get_container(pool1)

        # Create dataset in cont1
        obj_list = self._gen_dataset(cont1)

        # Clone cont1 to a new cont2 in pool1
        result = self.run_datamover(
            self.test_id + " (cont1->cont2) (same pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool1, None)
        cont2_label = self.parse_create_cont_label(result.stdout_text)

        # Verify data in cont2
        cont2 = get_existing_container(self, pool1, cont2_label)
        self._verify_dataset(obj_list, cont2)

        # Create pool2
        pool2 = self.get_pool()

        # Clone cont1 to a new cont3 in pool2
        result = self.run_datamover(
            self.test_id + " (cont1->cont3) (different pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool2, None)
        cont3_label = self.parse_create_cont_label(result.stdout_text)
        # Verify data in cont3
        cont3 = get_existing_container(self, pool2, cont3_label)
        self._verify_dataset(obj_list, cont3)

    def run_dm_obj_small_dsync(self):
        """
        Test Description:
            Tests syncing a small container at the object level.
        Use Cases:
            Create pool1.
            Create cont1 and an empty cont2 in pool1.
            Create a small dataset in cont1.
            Sync cont1 to cont2.
            Sync a second time, so the comparison of the destination runs
            against a populated container rather than an empty one.
        """
        self.set_tool("DSYNC")

        pool1 = self.get_pool()
        cont1 = self.get_container(pool1)

        # dsync does not create the destination
        cont2 = self.get_container(pool1)

        obj_list = self._gen_dataset(cont1)

        for run in ("first", "second"):
            self.run_datamover(
                self.test_id + " (cont1->cont2) ({} sync)".format(run),
                "DAOS_UUID", None, pool1, cont1,
                "DAOS_UUID", None, pool1, cont2)
            self._verify_dataset(obj_list, cont2)

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_dcp(self):
        """
        Test Description:
            DAOS-6858: Verify cloning a small container.
        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp
        :avocado: tags=DmvrObjSmallTest,test_dm_obj_small_dcp
        """
        self.run_dm_obj_small("DCP")

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_cont_clone(self):
        """
        Test Description:
            DAOS-6858: Verify cloning a small container.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=datamover,daos_cont_clone,daos_cmd
        :avocado: tags=DmvrObjSmallTest,test_dm_obj_small_cont_clone
        """
        self.run_dm_obj_small("CONT_CLONE")

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_small_dsync(self):
        """
        Test Description:
            Verify syncing a small container at the object level.
        :avocado: tags=all,daily_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dsync
        :avocado: tags=DmvrObjSmallTest,test_dm_obj_small_dsync
        """
        self.run_dm_obj_small_dsync()

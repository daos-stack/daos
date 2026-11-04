'''
  (C) Copyright 2026 Hewlett Packard Enterprise Development LP

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import avocado
from data_mover_test_base import DataMoverTestBase
from pydaos.raw import DaosApiError
from test_utils_container import get_existing_container


class DmvrObjEcTest(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Object Data Mover validation for cloning erasure coded containers.

    Test Class Description:
        Cloning a container whose objects are erasure coded and hold punched
        extents. Enumeration of an erasure coded object is served by a parity
        shard, where one parity block stands for a whole stripe, so the listed
        extents are an upper bound on the data and cover the punched records.
        A clone that copies the listed extents instead of what the fetch
        actually returned writes uninitialized bytes over those holes.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super().setUp()

        self.num_objs = self.params.get("num_objs", "/run/dataset/*")
        self.num_dkeys = self.params.get("num_dkeys", "/run/dataset/*")
        self.num_akeys_array = self.params.get("num_akeys_array", "/run/dataset/*")
        self.num_akeys_single = self.params.get("num_akeys_single", "/run/dataset/*")
        self.akey_sizes = self.params.get("akey_sizes", "/run/dataset/*")
        self.akey_extents = self.params.get("akey_extents", "/run/dataset/*")
        self.punch_extents = self.params.get("punch_extents", "/run/dataset/*")
        # an EC class is required, a replicated one would make the test vacuous
        self.obj_class = self.params.get("obj_class", "/run/dataset/*", "OC_EC_2P1G1")

    def run_dm_obj_ec(self, tool):
        """
        Test Description:
            Tests cloning a container of erasure coded objects with holes.
        Use Cases:
            Create pool1 and cont1.
            Create a dataset of erasure coded objects in cont1, where each
            array akey spans whole stripes and then has its leading records
            punched back out.
            Clone cont1 to a new cont2 and verify that the holes are still
            holes in cont2.

        Args:
            tool (str): the tool to use. Must be in DataMoverTestBase.TOOLS
        """
        self.set_tool(tool)

        pool1 = self.get_pool()
        cont1 = self.get_container(pool1)

        obj_list = self.dataset_gen(
            cont1,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents,
            oclass=self.obj_class, punch_extents=self.punch_extents)

        # the source itself must report the punched records as holes, otherwise
        # the run below would pass without ever exercising the hole handling
        self.dataset_verify(
            obj_list, cont1,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents,
            punch_extents=self.punch_extents)

        result = self.run_datamover(
            self.test_id + " (cont1->cont2) (same pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool1, None)
        cont2_label = self.parse_create_cont_label(result.stdout_text)

        cont2 = get_existing_container(self, pool1, cont2_label)
        self.dataset_verify(
            obj_list, cont2,
            self.num_objs, self.num_dkeys, self.num_akeys_single,
            self.num_akeys_array, self.akey_sizes, self.akey_extents,
            punch_extents=self.punch_extents)

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_ec_cont_clone(self):
        """
        Test Description:
            Verify cloning an erasure coded container with punched extents.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=datamover,daos_cont_clone,daos_cmd
        :avocado: tags=DmvrObjEcTest,test_dm_obj_ec_cont_clone
        """
        self.run_dm_obj_ec("CONT_CLONE")

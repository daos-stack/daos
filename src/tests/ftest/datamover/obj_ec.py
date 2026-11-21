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
    """Object Data Mover validation for erasure coded containers.

    Test Class Description:
        Copying a container whose objects are erasure coded and hold punched
        stripes. Enumeration of an erasure coded object is served by a parity
        shard, where one parity block stands for a whole stripe, so a stripe
        that has been punched is still reported as holding data. A copy that
        trusts the listed extents instead of the io map the fetch returned
        writes over those holes.
    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        super().setUp()

        self.num_objs = self.params.get("num_objs", "/run/dataset/*")
        self.num_dkeys = self.params.get("num_dkeys", "/run/dataset/*")
        self.num_akeys = self.params.get("num_akeys", "/run/dataset/*")
        self.stripe_size = self.params.get("stripe_size", "/run/dataset/*")
        self.num_stripes = self.params.get("num_stripes", "/run/dataset/*")
        self.punch_stripes = self.params.get("punch_stripes", "/run/dataset/*")
        self.full_punch_akeys = self.params.get("full_punch_akeys", "/run/dataset/*", 0)
        # an EC class is required, a replicated one would make the test vacuous
        self.obj_class = self.params.get("obj_class", "/run/dataset/*", "OC_EC_2P1G1")

    def _gen_dataset(self, cont):
        """Create the punched EC dataset and confirm the source reads back as expected.

        Args:
            cont (TestContainer): the container to create the dataset in.

        Returns:
            list: a list of DaosObj created.

        """
        obj_list = self.dataset_gen_ec(
            cont, self.num_objs, self.num_dkeys, self.num_akeys, self.obj_class,
            self.stripe_size, self.num_stripes, self.punch_stripes,
            full_punch_akeys=self.full_punch_akeys)

        # the source itself must report the punched stripes as holes, otherwise
        # the run below would pass without ever exercising the hole handling
        self._verify_dataset(obj_list, cont)

        return obj_list

    def _verify_dataset(self, obj_list, cont):
        """Verify a dataset created by _gen_dataset.

        Args:
            obj_list (list): obj_list returned from _gen_dataset.
            cont (TestContainer): the container to verify.
        """
        self.dataset_verify_ec(
            obj_list, cont, self.num_objs, self.num_dkeys, self.num_akeys,
            self.stripe_size, self.num_stripes, self.punch_stripes,
            full_punch_akeys=self.full_punch_akeys)

    def run_dm_obj_ec(self, tool):
        """
        Test Description:
            Tests copying a container of erasure coded objects with holes.
        Use Cases:
            Create pool1 and cont1.
            Create a dataset of erasure coded objects in cont1, where each
            array akey spans whole stripes and then has alternate stripes
            punched back out.
            Copy cont1 to a new cont2 and verify that the holes are still
            holes in cont2.

        Args:
            tool (str): the tool to use. Must be in DataMoverTestBase.TOOLS
        """
        self.set_tool(tool)

        pool1 = self.get_pool()
        cont1 = self.get_container(pool1)

        obj_list = self._gen_dataset(cont1)

        result = self.run_datamover(
            self.test_id + " (cont1->cont2) (same pool)",
            "DAOS_UUID", None, pool1, cont1,
            "DAOS_UUID", None, pool1, None)
        cont2_label = self.parse_create_cont_label(result.stdout_text)

        cont2 = get_existing_container(self, pool1, cont2_label)
        self._verify_dataset(obj_list, cont2)

    def run_dm_obj_ec_dsync(self):
        """
        Test Description:
            Tests syncing a container of erasure coded objects with holes.
        Use Cases:
            Create pool1, cont1 and an empty cont2.
            Create a dataset of erasure coded objects with punched stripes in cont1.
            Sync cont1 to cont2 and verify the holes are still holes in cont2.
            Sync a second time, so the comparison of the destination runs against
            a populated container rather than an empty one.
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

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_ec_dcp(self):
        """
        Test Description:
            Verify copying an erasure coded container with punched extents.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=datamover,mfu,mfu_dcp
        :avocado: tags=DmvrObjEcTest,test_dm_obj_ec_dcp
        """
        self.run_dm_obj_ec("DCP")

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_ec_dserialize(self):
        """
        Test Description:
            Verify serializing an erasure coded container with punched extents.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=datamover,mfu,mfu_serialize,mfu_deserialize,hdf5
        :avocado: tags=DmvrObjEcTest,test_dm_obj_ec_dserialize
        """
        self.run_dm_obj_ec("DSERIAL")

    @avocado.fail_on(DaosApiError)
    def test_dm_obj_ec_dsync(self):
        """
        Test Description:
            Verify syncing an erasure coded container with punched extents.
        :avocado: tags=all,daily_regression
        :avocado: tags=hw,medium
        :avocado: tags=datamover,mfu,mfu_dsync
        :avocado: tags=DmvrObjEcTest,test_dm_obj_ec_dsync
        """
        self.run_dm_obj_ec_dsync()

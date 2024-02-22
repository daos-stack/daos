'''
  (C) Copyright 2020-2024 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
from os.path import join

import avocado
from data_mover_test_base import DataMoverTestBase
from duns_utils import format_path
from pydaos.raw import DaosApiError


class DmvrDstCreate(DataMoverTestBase):
    # pylint: disable=too-many-ancestors
    """Data Mover validation for destination container create logic.

    Test Class Description:
        Tests the following cases:
            Destination container automatically created.
            Destination container user attributes match source.
            Destination container properties match source.

    :avocado: recursive
    """

    def setUp(self):
        """Set up each test case."""
        # Start the servers and agents
        super().setUp()

        # Get the parameters
        self.ior_flags = self.params.get("ior_flags", "/run/ior/*")
        self.test_file = self.ior_cmd.test_file.value

        # For dataset_gen and dataset_verify
        self.obj_list = []

    def run_dm_dst_create(self, tool, cont_type, api, check_props):
        """
        Test Description:
            Tests Data Mover destination container creation.
        Use Cases:
            Create pool1.
            Create POSIX cont1 in pool1.
            Create small dataset in cont1.
            Copy cont1 to a new cont in pool1, without a supplied cont.
            Create pool2.
            Copy cont1 to a new cont in pool2, without a supplied cont.
            For each copy, very container properties and user attributes.
            Repeat, but with container type Unknown.
        """
        # Set the tool to use
        self.set_tool(tool)

        # Set the api to use
        self.set_api(api)

        # Create 1 pool
        pool1 = self.create_pool()
        pool1.connect(2)

        # Create a source cont
        cont1 = self.get_container(pool1, type=cont_type)

        # Create source data
        src_props = self.write_cont(cont1)

        result = self.run_datamover(
            self.test_id + " cont1 to cont2 (same pool) (empty cont)",
            src_path=format_path(pool1, cont1),
            dst_path=format_path(pool1))
        cont2_label = self.parse_create_cont_label(result.stdout_text)
        cont2 = self.get_cont(pool1, cont2_label)
        cont2.type.update(cont1.type.value, "type")
        self.verify_cont(cont2, api, check_props, src_props)

        result = self.run_datamover(
            self.test_id + " cont1 to cont3 (same pool) (new cont label)",
            src_path=format_path(pool1, cont1),
            dst_path=format_path(pool1, 'cont3_label'))
        cont3_label = self.parse_create_cont_label(result.stdout_text)
        cont3 = self.get_cont(pool1, cont3_label)
        cont3.type.update(cont1.type.value, "type")
        self.verify_cont(cont3, api, check_props, src_props)

        special_label = 'cont:.-_'
        result = self.run_datamover(
            self.test_id + " cont1 to cont:.-_ (same pool) (new cont label with special chars)",
            src_path=format_path(pool1, cont1),
            dst_path=format_path(pool1, special_label))
        cont3_label = self.parse_create_cont_label(result.stdout_text)
        cont3 = self.get_cont(pool1, cont3_label)
        if cont3.label.value != special_label:
            self.fail(f'Expected dst cont label "{special_label}" but got "{cont3.label.value}"')
        cont3.type.update(cont1.type.value, "type")
        self.verify_cont(cont3, api, check_props, src_props)

        # Create another pool
        pool2 = self.create_pool()
        pool2.connect(2)

        result = self.run_datamover(
            self.test_id + " cont1 to cont4 (different pool) (empty cont)",
            src_path=format_path(pool1, cont1),
            dst_path=format_path(pool2))
        cont4_label = self.parse_create_cont_label(result.stdout_text)
        cont4 = self.get_cont(pool2, cont4_label)
        cont4.type.update(cont1.type.value, "type")
        self.verify_cont(cont4, api, check_props, src_props)

        # Only test POSIX paths with DFS API
        if api == "DFS":
            # Create a posix source path
            posix_path = join(self.new_posix_test_path(), self.test_file)
            self.run_ior_with_params("POSIX", posix_path, flags=self.ior_flags[0])

            result = self.run_datamover(
                self.test_id + " posix to cont6 (empty cont)",
                src_path=posix_path,
                dst_path=format_path(pool1))
            cont6_label = self.parse_create_cont_label(result.stdout_text)
            cont6 = self.get_cont(pool1, cont6_label)
            cont6.type.update(cont1.type.value, "type")
            self.verify_cont(cont6, api, False)

        pool1.disconnect()
        pool2.disconnect()

    def write_cont(self, cont):
        """Write the test data using either ior or the obj API.

        Args:
            cont (TestContainer): the container to write to.

        Returns:
            list: string list of properties from daos command.

        """
        if cont.type.value == "POSIX":
            self.run_ior_with_params(
                "DAOS", "/" + self.test_file, cont.pool, cont, flags=self.ior_flags[0])
        else:
            self.obj_list = self.dataset_gen(cont, 1, 1, 1, 0, [1024], [])

        # Write the user attributes
        cont.open()
        attrs = self.get_cont_usr_attr()
        cont.container.set_attr(attrs)
        cont.close()

        # Return existing cont properties
        return cont.get_prop()["response"]

    def verify_cont(self, cont, api, check_attr_prop=True, prop_list=None):
        """Read-verify test data using either ior or the obj API.

        Args:
            cont (TestContainer): the container to verify.
            check_attr_prop (bool, optional): whether to verify user
                attributes and cont properties. Defaults to False.
            prop_list (list, optional): list of properties from daos cont get-prop.
                Required when check_attr_prop is True.

        """
        # It's important to check the properties first, since when ior
        # mounts DFS the allocated OID might be incremented.
        if check_attr_prop:
            cont.open()
            self.verify_cont_prop(cont, prop_list, api)
            self.verify_usr_attr(cont)
            cont.close()

        if cont.type.value == "POSIX":
            # Verify POSIX containers copied with the DFS and Object APIs
            self.run_ior_with_params(
                "DAOS", "/" + self.test_file, cont.pool, cont, flags=self.ior_flags[1])
        else:
            # Verify non-POSIX containers copied with the Object API
            self.dataset_verify(self.obj_list, cont, 1, 1, 1, 0, [1024], [])

    def verify_cont_prop(self, cont, prop_list, api):
        """Verify container properties against an input list.
        Expects the container to be open.

        Args:
            cont (TestContainer): the container to verify.
            prop_list (list): list of properties from daos cont get-prop.
        """
        actual_list = cont.get_prop()["response"]

        # Make sure sizes match
        if len(prop_list) != len(actual_list):
            self.log.info("Expected\n%s\nbut got\n%s\n", prop_list, actual_list)
            self.fail("Container property verification failed.")

        # Make sure each property matches
        for prop_idx, prop in enumerate(prop_list):
            # Labels must be unique
            if prop['name'] == 'label':
                continue
            # This one is not set
            if api == "DFS" and "OID" in prop["description"]:
                continue
            if prop != actual_list[prop_idx]:
                self.log.info("Expected\n%s\nbut got\n%s\n", prop_list, actual_list)
                self.fail("Container property verification failed.")

        self.log.info("Verified %d container properties:\n%s", len(actual_list), actual_list)

    @staticmethod
    def get_cont_usr_attr():
        """Generate some user attributes"""
        return {
            "attr1".encode("utf-8"): "value 1".encode("utf-8"),
            "attr2".encode("utf-8"): "value 2".encode("utf-8")
        }

    def verify_usr_attr(self, cont):
        """Verify user attributes. Expects the container to be open.

        Args:
            cont (TestContainer): the container to verify.

        """
        attrs = self.get_cont_usr_attr()
        actual_attrs = cont.container.get_attr(list(attrs.keys()))

        # Make sure the sizes match
        if len(attrs.keys()) != len(actual_attrs.keys()):
            self.log.info("Expected\n%s\nbut got\n%s\n", attrs.items(), actual_attrs.items())
            self.fail("Container user attributes verification failed.")

        # Make sure each attr matches
        for attr, val in attrs.items():
            if attr not in actual_attrs:
                self.log.info("Expected\n%s\nbut got\n%s\n", attrs.items(), actual_attrs.items())
                self.fail("Container user attributes verification failed.")
            if val != actual_attrs[attr]:
                self.log.info("Expected\n%s\nbut got\n%s\n", attrs.items(), actual_attrs.items())
                self.fail("Container user attributes verification failed.")
        self.log.info("Verified %d user attributes:\n%s", len(attrs.keys()), attrs.items())

    @avocado.fail_on(DaosApiError)
    def test_dm_dst_create_dcp_posix_dfs(self):
        """
        Test Description:
            Verifies destination container creation
            for DFS API, including
            container properties and user attributes.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp,dfs,ior
        :avocado: tags=DmvrDstCreate,test_dm_dst_create_dcp_posix_dfs
        """
        self.run_dm_dst_create("DCP", "POSIX", "DFS", True)

    @avocado.fail_on(DaosApiError)
    def test_dm_dst_create_dcp_posix_daos(self):
        """
        Test Description:
            Verifies destination container creation
            for POSIX containers using OBJ API, including
            container properties and user attributes.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp
        :avocado: tags=DmvrDstCreate,test_dm_dst_create_dcp_posix_daos
        """
        self.run_dm_dst_create("DCP", "POSIX", "DAOS", True)

    @avocado.fail_on(DaosApiError)
    def test_dm_dst_create_dcp_unknown_daos(self):
        """
        Test Description:
            Verifies destination container creation
            when API is unknown, including
            container properties and user attributes.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,mfu,mfu_dcp
        :avocado: tags=DmvrDstCreate,test_dm_dst_create_dcp_unknown_daos
        """
        self.run_dm_dst_create("DCP", None, "DAOS", True)

    @avocado.fail_on(DaosApiError)
    def test_dm_dst_create_fs_copy_posix_dfs(self):
        """
        Test Description:
            Verifies destination container creation
            when API is unknown, including
            container properties and user attributes.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=datamover,daos_fs_copy,dfs,ior,daos_cmd
        :avocado: tags=DmvrDstCreate,test_dm_dst_create_fs_copy_posix_dfs
        """
        self.run_dm_dst_create("FS_COPY", "POSIX", "DFS", False)

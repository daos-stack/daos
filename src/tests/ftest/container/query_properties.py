'''
  (C) Copyright 2018-2023 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import ctypes

from pydaos.raw import daos_cref, DaosApiError, conversion, DaosContPropEnum

from apricot import TestWithServers


class QueryPropertiesTest(TestWithServers):
    """
    Test Class Description: Verify container properties are set during container query
    over pydaos API.

    :avocado: recursive
    """

    def test_query_properties(self):
        """JIRA ID: DAOS-9515

        Test Description: Verify container properties are correctly set by configuring
        some properties during create.

        Use Cases:
        1. Create a container with some properties related to checksum, type, etc.
        configured.
        2. Call container query. it returns container info such as UUID, snapshots, etc.,
        but not properties. We verify the property by passing in an empty data structure
        as pass by reference.
        3. Verify the property data structure passed in during step 2.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container
        :avocado: tags=query_properties,test_query_properties
        """
        errors = []

        self.pool = self.get_pool()
        self.container = self.get_container(self.pool)

        # Open the container.
        self.container.open()

        # Prepare the DaosProperty data structure that stores the values that are
        # configured based on the properties we used during create. Here, we create
        # the empty data structure and set the dpe_type fields. The values (dpe_val) will
        # be filled during query. See DaosContainer.create() and DaosContProperties for
        # more details.

        # If DaosContProperties.type is not "Unknown", there will be 4 elements.

        # Element 0: Layout type, which is determined by the container type. If the
        # container type is POSIX, we expect the value to be
        # DaosContPropEnum.DAOS_PROP_CO_LAYOUT_POSIX.value

        # Note: enable_chksum needs to be set to True to get the following 3 elements.
        # Element 1: Checksum. In default, we expect it to be 1.
        # Element 2: Checksum server verify. Since we updated the srv_verify to True, we
        # expect the value to be 1.
        # Element 3: Checksum chunk size. In default we expect it to be 16384.
        cont_prop = daos_cref.DaosProperty(4)

        cont_prop.dpp_entries[0].dpe_type = ctypes.c_uint32(
            DaosContPropEnum.DAOS_PROP_CO_LAYOUT_TYPE.value)
        cont_prop.dpp_entries[1].dpe_type = ctypes.c_uint32(
            DaosContPropEnum.DAOS_PROP_CO_CSUM.value)
        cont_prop.dpp_entries[2].dpe_type = ctypes.c_uint32(
            DaosContPropEnum.DAOS_PROP_CO_CSUM_SERVER_VERIFY.value)
        cont_prop.dpp_entries[3].dpe_type = ctypes.c_uint32(
            DaosContPropEnum.DAOS_PROP_CO_CSUM_CHUNK_SIZE.value)

        try:
            cont_info = self.container.container.query(cont_prop=cont_prop)
        except DaosApiError as error:
            self.log.info("Container query error! %s", error)

        # Sanity check that query isn't broken.
        uuid_query = conversion.c_uuid_to_str(cont_info.ci_uuid)
        uuid_create = self.container.container.get_uuid_str()
        if uuid_query != uuid_create:
            msg = ("Container UUID obtained after create and after query don't match! "
                   "Create: {}, Query: {}".format(uuid_create, uuid_query))
            errors.append(msg)

        # Verify values set in cont_prop.
        chksum_type_exp = self.params.get("chksum_type", "/run/expected_properties/*")
        srv_verify_exp = self.params.get("srv_verify", "/run/expected_properties/*")
        cksum_size_exp = self.params.get("cksum_size", "/run/expected_properties/*")

        # Verify layout type.
        actual_layout_type = cont_prop.dpp_entries[0].dpe_val
        expected_layout_type = DaosContPropEnum.DAOS_PROP_CO_LAYOUT_POSIX.value
        if actual_layout_type != expected_layout_type:
            msg = "Layout type is not POSIX! Expected = {}; Actual = {}".format(
                expected_layout_type, actual_layout_type)
            errors.append(msg)

        # Verify checksum.
        if cont_prop.dpp_entries[1].dpe_val != chksum_type_exp:
            msg = "Unexpected checksum from query! Expected = {}; Actual = {}".format(
                chksum_type_exp, cont_prop.dpp_entries[1].dpe_val)
            errors.append(msg)

        # Verify server verify.
        if cont_prop.dpp_entries[2].dpe_val != srv_verify_exp:
            msg = ("Unexpected server verify from query! "
                   "Expected = {}; Actual = {}".format(
                       srv_verify_exp, cont_prop.dpp_entries[2].dpe_val))
            errors.append(msg)

        # Verify checksum chunk size.
        if cont_prop.dpp_entries[3].dpe_val != cksum_size_exp:
            msg = ("Unexpected checksum chunk size from query! "
                   "Expected = {}; Actual = {}".format(
                       cksum_size_exp, cont_prop.dpp_entries[3].dpe_val))
            errors.append(msg)

        if errors:
            self.fail("\n----- Errors detected! -----\n{}".format("\n".join(errors)))

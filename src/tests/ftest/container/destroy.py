#!/usr/bin/python3
"""
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import traceback
import uuid

from apricot import TestWithServers
from pydaos.raw import DaosApiError


class ContainerDestroyTest(TestWithServers):
    """
    Tests DAOS container destroy.
    :avocado: recursive
    """

    def test_container_destroy(self):
        """
        Test destroy with valid/invalid UUID, pool handle, force flag, and open/not opened
        state.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=container,smoke
        :avocado: tags=container_destroy
        """
        expected_for_param = []
        change_result_uuid = self.params.get(
            "change_result", '/run/destroy_variants/destroy_uuid/*/')
        change_uuid = change_result_uuid[0]
        expected_for_param.append(change_result_uuid[1])

        validity_result_poh = self.params.get(
            "poh", '/run/destroy_variants/destroy_pool_handle/*/')
        poh_validity = validity_result_poh[0]
        expected_for_param.append(validity_result_poh[1])

        open_result = self.params.get(
            "opened", "/run/destroy_variants/connection_open/*/")
        open_container = open_result[0]
        expected_for_param.append(open_result[1])

        force_result = self.params.get("force", "/run/destroy_variants/force_destroy/*/")
        force = force_result[0]

        # force=0 in .yaml file specifies FAIL, but if not opened, force=0 is expected
        # to pass.
        if force == 0 and not open_container:
            expected_for_param.append('PASS')
        else:
            expected_for_param.append(force_result[1])

        # opened=True in .yaml file specifies PASS, however
        # if it is also the case force=0, then FAIL is expected

        expected_result = 'PASS'
        for result in expected_for_param:
            if result == 'FAIL':
                expected_result = 'FAIL'
                break

        passed = False

        # Create a pool and a container.
        self.add_pool()
        self.add_container(pool=self.pool)

        # Open the container if required
        if open_container:
            self.container.open()

        # Update pool handle based on the variant.
        if poh_validity == 'INVALID':
            poh = 99999
        else:
            poh = self.pool.pool.handle

        uuid_str = None
        # Update container UUID used during destroy based on the variant.
        if change_uuid:
            uuid_str = str(uuid.uuid4())

        try:
            self.container.container.destroy(force=force, poh=poh, uuid_str=uuid_str)
            passed = True
        except DaosApiError:
            self.log.info(traceback.format_exc())

            # If container destroy failed, the tearDown process will try to destroy it.
            # However, the invalid pool handle and the container UUID stick to the member
            # of DaosContainer instance, so the tearDown will fail. We should destroy it
            # here before getting to tearDown. To do so, set uuid and poh in the
            # DaosContainer instance. Then call destroy on the TestContainer instance.
            self.container.container.poh = self.pool.pool.handle
            self.container.destroy()

        if expected_result == 'PASS' and not passed:
            self.fail("Test was expected to pass but it failed.")
        if expected_result == 'FAIL' and passed:
            self.fail("Test was expected to fail but it passed.")

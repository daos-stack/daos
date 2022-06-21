#!/usr/bin/python3
'''
  (C) Copyright 2018-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
'''
import traceback
import ctypes
from avocado.core.exceptions import TestFail
from apricot import TestWithServers


class BadConnectTest(TestWithServers):
    """Test pool connect with different UUIDs.
    :avocado: recursive
    """
    def test_connect(self):
        """
        Test pool connect with valid and invalid UUIDs. Invalid UUIDs are None and the
        invalid value.

        :avocado: tags=all,full_regression
        :avocado: tags=vm
        :avocado: tags=pool
        :avocado: tags=bad_connect,test_connect
        """
        self.add_pool(connect=False)

        # save this uuid since we might trash it as part of the test
        original_uuid = (ctypes.c_ubyte * 16)()
        ctypes.memmove(original_uuid, self.pool.pool.uuid, 16)

        uuid_result = self.params.get("uuid", '/run/uuids/*/')
        uuid = uuid_result[0]
        expected_result = uuid_result[1]

        # Set invalid UUID, or keep it to test the valid case.
        if uuid != "VALID":
            if uuid == "None":
                self.pool.pool.uuid = None
            else:
                self.pool.pool.uuid[4] = int(uuid)

        try:
            self.pool.connect()
            if expected_result == "FAIL":
                self.fail("Test was expected to fail but it passed.")
        except TestFail as excep:
            print(excep)
            print(traceback.format_exc())
            if expected_result == "PASS":
                self.fail("Test was expected to pass but it failed.")
        # cleanup the pool
        finally:
            if self.pool is not None and self.pool.pool.attached == 1:
                # restore values in case we trashed them during test
                if self.pool.pool.uuid is None:
                    self.pool.pool.uuid = (ctypes.c_ubyte * 16)()
                ctypes.memmove(self.pool.pool.uuid, original_uuid, 16)
                print("pool uuid after restore {}".format(
                    self.pool.pool.get_uuid_str()))

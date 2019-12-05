#!/usr/bin/python
'''
  (C) Copyright 2017-2019 Intel Corporation.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
  The Government's rights to use, modify, reproduce, release, perform, display,
  or disclose this software are subject to the terms of the Apache License as
  provided in Contract No. B609815.
  Any reproduction of computer software, computer software documentation, or
  portions thereof marked with this legend must also reproduce the markings.
'''
from __future__ import print_function

import time
import traceback
from avocado import main
from apricot import TestWithServers

from pydaos.raw import DaosApiError, c_uuid_to_str
from general_utils import get_pool, get_container


# pylint: disable=broad-except
class SimpleCreateDeleteTest(TestWithServers):
    """
    Tests DAOS container basics including create, destroy, open, query
    and close.

    :avocado: recursive
    """

    def test_container_basics(self):
        """
        Test basic container create/destroy/open/close/query.

        :avocado: tags=all,container,pr,medium,basecont
        """
        try:
            # Parameters used in pool create
            pool_mode = self.params.get("mode", '/run/pool/createmode/')
            pool_name = self.params.get("setname", '/run/pool/createset/')
            pool_size = self.params.get("size", '/run/pool/createsize/')

            # Create pool and connect
            self.pool = get_pool(
                self.context, pool_mode, pool_size, pool_name, 1, self.d_log)

            # Create a container and open
            self.container = get_container(self.context, self.pool, self.d_log)

            # Query and compare the UUID returned from create with
            # that returned by query
            self.container.query()

            if self.container.get_uuid_str() != c_uuid_to_str(
                    self.container.info.ci_uuid):
                self.fail("Container UUID did not match the one in info'n")

            self.container.close()

            # Wait and destroy
            time.sleep(5)
            self.container.destroy()

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")
        except Exception as excep:
            self.fail("Daos code segfaulted most likely, error: %s" % excep)


if __name__ == "__main__":
    main()

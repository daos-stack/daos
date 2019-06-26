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
from apricot import TestWithoutServers


from agent_utils import run_agent, stop_agent
from server_utils import run_server, stop_server
from general_utils import get_pool, get_container
import write_host_file
from daos_api import DaosPool, DaosContainer, DaosApiError
from conversion import c_uuid_to_str

class SimpleCreateDeleteTest(TestWithoutServers):
    """
    Tests DAOS container basics including create, destroy, open, query
    and close.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        super(SimpleCreateDeleteTest, self).__init__(*args, **kwargs)
        self.agent_sessions = None

    def test_container_basics(self):
        """
        Test basic container create/destroy/open/close/query.  Nothing fancy
        just making sure they work at a rudimentary level

        :avocado: tags=pr,container,containercreate,containerdestroy,basecont
        """
        try:
            self.hostlist_servers = self.params.get("test_machines", '/run/hosts/*')
            self.hostfile_servers = write_host_file.write_host_file(
                                    self.hostlist_servers, self.workdir)

            self.agent_sessions = run_agent(self.basepath, self.hostlist_servers)
            run_server(self.hostfile_servers, self.server_group, self.basepath)

            # give it time to start
            time.sleep(2)

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
            container.query()

            if container.get_uuid_str() != c_uuid_to_str(
                    container.info.ci_uuid):
                self.fail("Container UUID did not match the one in info'n")

            container.close()

            # wait a few seconds and then destroy
            time.sleep(5)
            container.destroy()

        except DaosApiError as excep:
            print(excep)
            print(traceback.format_exc())
            self.fail("Test was expected to pass but it failed.\n")
        except Exception as excep:
            self.fail("Daos code segfaulted most likely, error: %s" % excep)
        finally:
            self.log.info("Clean up pool")
            if self.pool is not None:
                pool.disconnect()
                pool.destroy(1)
            if self.agent_sessions:
                stop_agent(self.agent_sessions)
            stop_server(hosts=self.hostlist_servers)

if __name__ == "__main__":
    main()

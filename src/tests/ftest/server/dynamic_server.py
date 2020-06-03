#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
"""
from apricot import TestWithServers
#from command_utils_base import CommandFailure


class DynamicServer(TestWithServers):
    """Test Class Description:
    Test to dynamically add a server, create a pool on it, kill it,
    and restart and add it back.
    :avocado: recursive
    """

    def test_dynamic_server_addition(self):
        """
        JIRA ID: DAOS-3598

        Test Description: Test dmg tool executes with variant positive and
        negative inputs to its configuration file.

        :avocado: tags=all,medium,control,full_regression,dynamic_server
        """
        extra_servers = self.params.get("test_servers", "/run/extra_servers/*")
        self.log.debug("## Extra Servers = %s", extra_servers)

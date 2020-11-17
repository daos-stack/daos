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


class ConfigGenerate(TestWithServers):
    """Test Class Description:

    Verify the veracity of the configuration created by the commmand and what
    the user specified, input verification and correct execution of the server
    once the generated configuration is propagated to the servers.

    Test requirements:
     - cluster must have both NVME and SCM
     - the storage setup

    :avocado: recursive
    """

    def generate_config(self):
        """ Verify that dmg can generate an accurate configuration file.

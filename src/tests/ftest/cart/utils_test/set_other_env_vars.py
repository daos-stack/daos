#!/usr/bin/python
'''
  (C) Copyright 2018-2021 Intel Corporation.

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

import sys
import subprocess

from apricot import TestWithoutServers

sys.path.append('./util')

# Can't all this import before setting sys.path
# pylint: disable=wrong-import-position
from cart_utils import CartUtils

class SetOtherEnvVars(TestWithoutServers):
    """
    Runs basic test of cart_utils.py:set_other_env_vars

    :avocado: recursive
    """
    def setUp(self):
        """ Test setup """
        print("Running setup\n")
        self.utils = CartUtils()

    def tearDown(self):
        """ Tear down """
        print("Running tearDown\n")

    def test_cart_no_pmix(self):
        """
        Test set_other_env_vars

        :avocado: tags=set_other_env_vars
        """

        print("Before set_other_env_vars\n")
        print(subprocess.check_output("env", shell=True))
        self.utils.set_other_env_vars(self)
        print("After set_other_env_vars\n")
        print(subprocess.check_output("env", shell=True))

        print("Before unset_other_env_vars\n")
        print(subprocess.check_output("env", shell=True))
        self.utils.unset_other_env_vars(self)
        print("After unset_other_env_vars\n")
        print(subprocess.check_output("env", shell=True))

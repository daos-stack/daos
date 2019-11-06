#!/usr/bin/python
"""
  (C) Copyright 2019 Intel Corporation.

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
import os

from apricot import TestWithServers
from command_utils import ExecutableCommand
from test_utils import TestPool

class IoConfGen(ExecutableCommand):
    """Defines an object for the daos_gen_io_conf and daos_run_io_conf commands.

    :avocado: recursive
    """
    def __init__(self, path="", env=None):
        """Create a ExecutableCommand object.

        Uses Avocado's utils.process module to run a command str provided.

        Args:
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file.
                Defaults to ""
        """
        super(IoConfGen, self).__init__("", path)
        self.verbose = True
        self.env = env

class IoConfTestBase(TestWithServers):
    """Base rebuild test class.

    :avocado: recursive
    """

    def __init__(self, *args, **kwargs):
        """Initialize a IoConfTestBase object."""
        super(IoConfTestBase, self).__init__(*args, **kwargs)

    def setup_test_pool(self):
        """Define a TestPool object."""
        self.pool = TestPool(self.context, self.log)
        self.pool.get_params(self)

    def run_gen_io_conf(self, filename="testfile"):
        """
        Generate the test data set file based on parameter from yaml file

        Args:
            filename (string): Filename (with/without path) for
            create the data set.
        """
        rank_size = self.params.get("ranks",
                                    "/run/daos_io_conf_run/no_of_ranks/*/")
        target_size = self.params.get("targets",
                                      "/run/daos_io_conf_run/tgts/*/")
        obj_num = self.params.get("obj_num",
                                   "/run/daos_io_conf_run/no_of_objects/*/")
        dkey_num = self.params.get("dkeys",
                                   "/run/daos_io_conf_run/no_of_dkeys/*/")
        akey_num = self.params.get("akeys",
                                   "/run/daos_io_conf_run/no_of_akeys/*/")
        rec_size = self.params.get("re_size",
                                   "/run/daos_io_conf_run/record_size/*/")
        obj_class = self.params.get("obj_class",
                                   "/run/daos_io_conf_run/object_class/*/")

        #Get the daos_gen_io_conf bin path.
        daos_gen_io_conf_bin = (os.path.join(self.prefix,
                                             "bin/daos_gen_io_conf"))
        cmd = [daos_gen_io_conf_bin,
               rank_size,
               target_size,
               obj_num,
               dkey_num,
               akey_num,
               rec_size,
               obj_class,
               filename]

        #Run daos_gen_io_conf
        #daos_gen_io_conf -g <rank_size> -t <tgt_size>-o [obj_num]
        #-d [dkey_num] -a [akey_num] -s [rec_size]-O obj_class <file_name>
        io_conf_gen = IoConfGen(" ".join(cmd))
        io_conf_gen.run()

    def run_io_conf(self, filename="testfile"):
        """ Run daos_run_io_conf <file_name> """
        #Get the daos_run_io_conf bin path.
        daos_run_io_conf_bin = (os.path.join(self.prefix,
                                             "bin/daos_run_io_conf"))
        cmd = [daos_run_io_conf_bin,
               filename]

        #Run daos_run_io_conf
        io_conf = IoConfGen(" ".join(cmd),
                                env={"POOL_SCM_SIZE":"{}"
                                     .format(self.pool.scm_size)})
        io_conf.run()

    def execute_io_conf_run_test(self):
        """
        Execute the rebuild test steps.
        """
        #Setup the pool
        self.setup_test_pool()
        #create test file using daos_gen_io_conf
        self.run_gen_io_conf()
        #Run test file using daos_run_io_conf
        self.run_io_conf()

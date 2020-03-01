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
import os
import random
from avocado.utils import process

from apricot import TestWithServers
from command_utils import ExecutableCommand, CommandFailure, FormattedParameter
from command_utils import BasicParameter
from test_utils_pool import TestPool


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
        super(IoConfGen, self).__init__("/run/gen_io_conf/*",
                                        "daos_gen_io_conf", path)
        self.verbose = True
        self.env = env
        self.ranks = FormattedParameter("-g {}")
        self.targets = FormattedParameter("-t {}")
        self.obj_num = FormattedParameter("-o {}")
        self.akeys = FormattedParameter("-a {}")
        self.dkeys = FormattedParameter("-d {}")
        self.record_size = FormattedParameter("-s {}")
        self.obj_class = FormattedParameter("-O {}")
        self.filename = BasicParameter(None, "testfile")

    def run_conf(self):
        """Run the daos_run_io_conf command as a foreground process.

        Raises:
            CommandFailure: if there is an error running the command

        """
        command = " ".join([os.path.join(self._path, "daos_run_io_conf"),
                            self.filename.value])
        kwargs = {
            "cmd": command,
            "timeout": self.timeout,
            "verbose": self.verbose,
            "allow_output_check": "combined",
            "shell": True,
            "env": self.env,
            "sudo": self.sudo,
        }
        try:
            # Block until the command is complete or times out
            return process.run(**kwargs)

        except process.CmdError as error:
            # Command failed or possibly timed out
            msg = "Error occurred running '{}': {}".format(command, error)
            self.log.error(msg)
            raise CommandFailure(msg)

def gen_unaligned_io_conf(record_size, filename="testfile"):
    """
    Generate the data-set file based on record size.

    Args:
        record_size(Number): Record Size to fill the data.
        filename (string): Filename (with/without path) for
                           creating the data set.
    """
    rand_ofs_end = random.randint(1, record_size - 1)
    rand_ofs_start = rand_ofs_end - 1
    file_data = (
        "test_lvl daos",
        "dkey dkey_0",
        "akey akey_0",
        "iod_size 1",
        "pool --query",
        "update --tx 0 --recx \"[0, {}]045\"".format(record_size),
        "update --tx 1 --recx \"[{}, {}]123\""
        .format(rand_ofs_start, rand_ofs_end),
        "fetch  --tx 1 -v --recx \"[0, {}]045 [{}, {}]123 [{}, {}]045\""
        .format(rand_ofs_start,
                rand_ofs_start,
                rand_ofs_end,
                rand_ofs_end,
                record_size),
        "pool --query")

    try:
        file_hd = open(filename, "w+")
        file_hd.write("\n".join(file_data))
        file_hd.close()
    except Exception as error:
        raise error

class IoConfTestBase(TestWithServers):
    """Base rebuild test class.

    :avocado: recursive
    """

    def setup_test_pool(self):
        """Define a TestPool object."""
        self.pool = TestPool(self.context, dmg_command=self.get_dmg_command())
        self.pool.get_params(self)

    def execute_io_conf_run_test(self):
        """
        Execute the rebuild test steps.
        """
        self.setup_test_pool()
        pool_env = {"POOL_SCM_SIZE": "{}".format(self.pool.scm_size)}
        io_conf = IoConfGen(os.path.join(self.prefix, "bin"), env=pool_env)
        io_conf.get_params(self)
        io_conf.run()
        #Run test file using daos_run_io_conf
        io_conf.run_conf()

    def unaligned_io(self):
        """
        Execute the unaligned IO test steps.
        """
        total_sizes = self.params.get("sizes", "/run/datasize/*")
        #Setup the pool
        self.setup_test_pool()
        pool_env = {"POOL_SCM_SIZE": "{}".format(self.pool.scm_size)}
        io_conf = IoConfGen(os.path.join(self.prefix, "bin"), env=pool_env)
        io_conf.get_params(self)
        for record_size in total_sizes:
            print("Start test for record size = {}".format(record_size))
            #create unaligned test data set
            gen_unaligned_io_conf(record_size)
            #Run test file using daos_run_io_conf
            io_conf.run_conf()

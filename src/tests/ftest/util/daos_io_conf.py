#!/usr/bin/python
"""
  (C) Copyright 2020-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os
import random

from apricot import TestWithServers
from command_utils import ExecutableCommand
from command_utils_base import \
    BasicParameter, FormattedParameter
from exception_utils import CommandFailure, MPILoadError
from env_modules import load_mpi
from job_manager_utils import Orterun


class IoConfGen(ExecutableCommand):
    """Defines an object for the daos_gen_io_conf and daos_run_io_conf commands.

    :avocado: recursive
    """

    def __init__(self, path="", filename="testfile", mpi_type="openmpi"):
        """Create a ExecutableCommand object.

        Uses Avocado's utils.process module to run a command str provided.

        Args:
            command (str): string of the command to be executed.
            path (str, optional): path to location of command binary file. Defaults to ""
            mpi_type (str, optional): MPI type to load or use with the job manager
        """
        super().__init__("/run/gen_io_conf/*", "daos_gen_io_conf", path)
        self.verbose = True
        self.ranks = FormattedParameter("-g {}")
        self.targets = FormattedParameter("-t {}")
        self.obj_num = FormattedParameter("-o {}")
        self.akeys = FormattedParameter("-a {}")
        self.dkeys = FormattedParameter("-d {}")
        self.record_size = FormattedParameter("-s {}")
        self.obj_class = FormattedParameter("-O {}")
        self.filename = BasicParameter(None, filename)
        self.mpi_type = mpi_type

    def run(self):
        """Run the command.

        Raises:
            CommandFailure: if there is an error running the command
            MPILoadError: if there is an error loading mpi

        """
        if not load_mpi(self.mpi_type):
            raise MPILoadError(self.mpi_type)

        return super().run()

    def run_conf(self, dmg_config_file):
        """Run the daos_run_io_conf command as a foreground process.

        Args:
            dmg_config_file: dmg file to run test.

        Return:
            Result bool: True if command success and false if any error.

        """
        success_msg = 'daos_run_io_conf completed successfully'
        command = " ".join([os.path.join(self._path, "daos_run_io_conf"), " -n ",
                            dmg_config_file, self.filename.value])

        manager = Orterun(command, mpi_type=self.mpi_type)
        # run daos_run_io_conf Command using Openmpi
        try:
            out = manager.run()

            # Return False if "ERROR" in stdout
            for line in out.stdout_text.splitlines():
                if 'ERROR' in line:
                    return False
            # Return False if not expected message to confirm test completed.
            if success_msg not in out.stdout_text.splitlines()[-1]:
                return False

        # Return False if Command failed.
        except CommandFailure:
            return False

        return True


def gen_unaligned_io_conf(record_size, filename="testfile"):
    """Generate the data-set file based on record size.

    Args:
        record_size(Number): Record Size to fill the data.
        filename (string): Filename (with/without path) for creating the data set.
    """
    rand_ofs_end = random.randint(1, record_size - 1) #nosec
    rand_ofs_start = rand_ofs_end - 1
    file_data = (
        "test_lvl daos",
        "dkey dkey_0",
        "akey akey_0",
        "iod_size 1",
        "pool --query",
        "update --tx 0 --recx \"[0, {}]045 \"".format(record_size),
        "update --tx 1 --recx \"[{}, {}]123 \"".format(
            rand_ofs_start, rand_ofs_end),
        "fetch  --tx 1 -v --recx \"[0, {0}]045 [{0}, {1}]123 [{1}, "
        "{2}]045\"".format(rand_ofs_start, rand_ofs_end, record_size),
        "pool --query\n")

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

    def __init__(self, *args, **kwargs):
        """Initialize a IoConfTestBase object."""
        super().__init__(*args, **kwargs)
        self.testfile = None
        self.dmg = None
        self.dmg_config_file = None

    def setup_test_pool(self):
        """Define a TestPool object."""
        self.add_pool(create=False)
        avocado_tmp_dir = os.environ['AVOCADO_TESTS_COMMON_TMPDIR']
        self.testfile = os.path.join(avocado_tmp_dir, 'testfile')
        self.dmg = self.get_dmg_command()
        self.dmg_config_file = self.dmg.yaml.filename

    def get_io_conf_gen(self):
        """Get a IoConfGen command object.

        Returns:
            IoConfGen: a configured IoConfGen object

        """
        mpi_type = self.params.get("mpi_type", "/run/gen_io_conf/*", "openmpi")
        io_conf = IoConfGen(os.path.join(self.prefix, "bin"), self.testfile, mpi_type)
        io_conf.set_environment({"POOL_SCM_SIZE": "{}".format(self.pool.scm_size)})
        io_conf.get_params(self)
        return io_conf

    def execute_io_conf_run_test(self):
        """Execute the rebuild test steps."""
        self.setup_test_pool()
        io_conf = self.get_io_conf_gen()
        io_conf.run()
        # Run test file using daos_run_io_conf
        if not io_conf.run_conf(self.dmg_config_file):
            self.fail("daos_run_io_conf failed")

    def unaligned_io(self):
        """Execute the unaligned IO test steps."""
        total_sizes = self.params.get("sizes", "/run/datasize/*")
        # Setup the pool
        self.setup_test_pool()
        io_conf = self.get_io_conf_gen()
        for record_size in total_sizes:
            print("Start test for record size = {}".format(record_size))
            # Create unaligned test data set
            gen_unaligned_io_conf(record_size, self.testfile)
            # Run test file using daos_run_io_conf
            if not io_conf.run_conf(self.dmg_config_file):
                self.fail("daos_run_io_conf failed")

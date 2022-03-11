#!/usr/bin/python
"""
  (C) Copyright 2019-2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
from sys import version_info
from command_utils_base import FormattedParameter
from command_utils import ExecutableCommand


class LLNLCommand(ExecutableCommand):
    """Defines an object from running the MPIIO llnl command."""

    def __init__(self, path):
        """Create an LLNLCommand object.

        Args:
            path (str): path to the macsio command.
        """
        super().__init__("/run/llnl/*", "testmpio_daos", path)
        self.input = FormattedParameter("{}", "1")


class Mpi4pyCommand(ExecutableCommand):
    """Defines an object from running the MPIIO mpi4py command."""

    def __init__(self, path):
        """Create an Mpi4pyCommand object.

        Args:
            path (str): path to the macsio command.
        """
        super().__init__("/run/mpi4py/*", "test_io_daos.py", path)

    def __str__(self):
        """Return the command with all of its defined parameters as a string.

        Returns:
            str: the command with all the defined parameters

        """
        return " ".join(["python{}".format(version_info.major), super().__str__()])


class RomioCommand(ExecutableCommand):
    """Defines an object from running the MPIIO romio command."""

    def __init__(self, path):
        """Create an RomioCommand object.

        Args:
            path (str, optional): path to the macsio command. Defaults to "".
        """
        super().__init__("/run/romio/*", "runtests", path)
        self.fname = FormattedParameter("-fname={}", "daos:/test1")
        self.subset = FormattedParameter("-subset", True)


class Hdf5Command(ExecutableCommand):
    """Defines an object from running the MPIIO hdf5 command."""

    def __init__(self, command, path):
        """Create an Hdf5Command object.

        Args:
            command (str): hdf5 command, e.g. testphdf5, t_shapesame
            path (str): path to the hdf5 command.
        """
        super().__init__("/run/hdf5/*", command, path)

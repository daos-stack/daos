"""
    (C) Copyright 2025 Hewlett Packard Enterprise Development LP

    SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import os

from command_utils import SubProcessCommand
from command_utils_base import BasicParameter, FormattedParameter
from exception_utils import CommandFailure
from general_utils import get_log_file

E3SMIO_INPUTS = {
    'e3sm_io_map_i_case_1344p.h5': os.path.join(
        os.path.realpath(os.path.join(os.path.dirname(__file__), '..')),
        'inputs', 'e3sm_io_map_i_case_1344p.h5')
}


def get_e3sm_io(test, manager, hosts, path, namespace="/run/e3sm_io/*", e3sm_io_params=None):
    """Get an E3SMIO object.

    Args:
        test (Test): avocado Test object
        manager (JobManager): command to manage the multi-host execution of e3sm_io
        hosts (NodeSet): hosts on which to run the e3sm_io command
        path (str): hostfile path.
        namespace (str, optional): path to yaml parameters. Defaults to "/run/e3sm_io/*".
        e3sm_io_params (dict, optional): dictionary of E3SMIOCommand attributes to override from
            get_params(). Defaults to None.

    Returns:
        E3SMIO: the E3SMIO object requested
    """
    e3sm_io = E3SMIO(test, manager, hosts, path, namespace)
    if e3sm_io_params:
        for name, value in e3sm_io_params.items():
            e3sm_io.update(name, value)
    return e3sm_io


def run_e3sm_io(test, manager, hosts, path, processes, ppn=None,
                namespace="/run/e3sm_io/*", working_dir=None, e3sm_io_params=None):
    """Run e3sm_io on multiple hosts.

    Args:
        test (Test): avocado Test object
        manager (JobManager): command to manage the multi-host execution of e3sm_io
        log (str): log file.
        hosts (NodeSet): hosts on which to run the e3sm_io command
        path (str): hostfile path.
        processes (int): number of processes to run
        ppn (int, optional): number of processes per node to run.  If specified it will override
            the processes input. Defaults to None.
        namespace (str, optional): path to yaml parameters. Defaults to "/run/e3sm_io/*".
        working_dir (str, optional): working directory for the e3sm_io command. Defaults to None.
        e3sm_io_params (dict, optional): dictionary of E3SMIOCommand attributes to override from
            get_params(). Defaults to None.

    Raises:
        CommandFailure: if there is an error running the e3sm_io command

    Returns:
        CmdResult: result of the e3sm_io command

    """
    e3sm_io = get_e3sm_io(test, manager, hosts, path, namespace, e3sm_io_params)
    return e3sm_io.run(processes, ppn, working_dir)


class E3SMIOCommand(SubProcessCommand):
    # pylint: disable=wrong-spelling-in-docstring,wrong-spelling-in-comment
    """Defines a object for executing an e3sm_io command.

    Example:
        >>> # Typical use inside of a DAOS avocado test method.
        >>> cmd = E3SMIOCommand(self.test_env.log_dir)
        >>> cmd.get_params(self)
        >>> mpirun = Mpirun()
        >>> server_manager = self.server_manager[0]
        >>> mpirun.assign_hosts(self.hostlist_clients, self.workdir, None)
        >>> mpirun.assign_processes(len(self.hostlist_clients))
        >>> mpirun.assign_environment(env)
        >>> mpirun.run()
    """

    def __init__(self, log_dir, namespace="/run/e3sm_io/*"):
        """Create an E3SMIOCommand object.

        Args:
            log_dir (str): directory in which to put log files
            namespace (str, optional): path to yaml parameters. Defaults to "/run/e3sm_io/*".
        """
        super().__init__(namespace, "e3sm_io", timeout=60)
        self._log_dir = log_dir

        # FILE: Name of input file storing data decomposition maps.
        self.input_file = BasicParameter(None, None, position=0)

        # [-v] Verbose mode
        self.verbose = FormattedParameter("-v", False)

        # [-k] Keep the output files when program exits (default: deleted)
        self.keep_output = FormattedParameter("-k", False)

        # [-r num] Number of time records/steps written in F case h1 file and I
        #          case h0 file (default: 1)
        self.num_steps = FormattedParameter("-r {}")

        # [-o path] Output file path (folder name when subfiling is used, file
        #           name otherwise).
        self.output_file = FormattedParameter("-o {}")

        # [-a api]  I/O library name
        #     pnetcdf:   PnetCDF library (default)
        #     netcdf4:   NetCDF-4 library
        #     hdf5:      HDF5 library
        #     hdf5_md:   HDF5 library using multi-dataset I/O APIs
        #     hdf5_log:  HDF5 library with Log VOL connector
        #     adios:     ADIOS library using BP3 format
        self.api = FormattedParameter("-a {}")

        # [-x strategy] I/O strategy
        #     canonical: Store variables in the canonical layout (default).
        #     log:       Store variables in the log-based storage layout.
        #     blob:      Pack and store all data written locally in a contiguous
        #                block (blob), ignoring variable's canonical order.
        self.strategy = FormattedParameter("-x {}")

        # Any other flags not represented directly
        self.flags = BasicParameter(None, None)

        # Include bullseye coverage file environment
        self.env["COVFILE"] = os.path.join(os.sep, "tmp", "test.cov")

        # Default log file
        self.env["D_LOG_FILE"] = get_log_file(f"{self.command}_daos.log")


class E3SMIO:
    """Defines a class that runs the e3sm_io command through a job manager, e.g. mpirun."""

    def __init__(self, test, manager, hosts, path=None, namespace="/run/e3sm_io/*"):
        """Initialize an E3SMIO object.

        Args:
            test (Test): avocado Test object
            manager (JobManager): command to manage the multi-host execution of e3sm_io
            hosts (NodeSet): hosts on which to run the e3sm_io command
            path (str, optional): hostfile path. Defaults to None.
            namespace (str, optional): path to yaml parameters. Defaults to "/run/e3sm_io/*".
        """
        self.manager = manager
        self.manager.assign_hosts(hosts, path)
        self.manager.job = E3SMIOCommand(test.test_env.log_dir, namespace)
        self.manager.job.get_params(test)
        self.manager.output_check = "both"
        self.timeout = test.params.get("timeout", namespace, None)
        self.label_generator = test.label_generator
        self.test_id = test.test_id
        self.env = self.command.env.copy()

    @property
    def command(self):
        """Get the E3SMIOCommand object.

        Returns:
            E3SMIOCommand: the E3SMIOCommand object managed by the JobManager

        """
        return self.manager.job

    def update(self, name, value):
        """Update a E3SMIOCommand BasicParameter with a new value.

        Args:
            name (str): name of the E3SMIOCommand BasicParameter to update
            value (str): value to assign to the E3SMIOCommand BasicParameter
        """
        param = getattr(self.command, name, None)
        if param:
            if isinstance(param, BasicParameter):
                param.update(value, ".".join([self.command.command, name]))
            else:
                raise ValueError(
                    f"{name} is not a BasicParameter of the {self.command.command} command")

    def run(self, processes, ppn=None, working_dir=None):
        """Run e3sm_io.

        Args:
            processes (int): number of processes to run
            ppn (int, optional): number of processes per node to run.  If specified it will override
                the processes input. Defaults to None.
            working_dir (str, optional): working directory for the e3sm_io command.
                Defaults to None.

        Raises:
            CommandFailure: if there is an error running the e3sm_io command

        Returns:
            CmdResult: result of the e3sm_io command

        """
        # Pass only processes or ppn to be compatible with previous behavior
        if ppn is not None:
            self.manager.assign_processes(ppn=ppn)
        else:
            self.manager.assign_processes(processes=processes)

        self.manager.assign_environment(self.env)

        if working_dir is not None:
            self.manager.update_params(working_dir=working_dir)

        try:
            return self.manager.run()
        except CommandFailure as error:
            error_message = "e3sm_io Failed:\n  {}".format("\n  ".join(str(error).split("\n")))
            raise CommandFailure(error_message) from error

    def stop(self):
        """Stop the e3sm_io command when the job manager was run as a subprocess .

        Raises:
            CommandFailure: if there is an error stopping the e3sm_io subprocess

        """
        if not self.manager.run_as_subprocess:
            return
        try:
            self.manager.stop()
        except CommandFailure as error:
            raise CommandFailure(f"e3sm_io Failed: {error}") from error

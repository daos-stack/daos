#!/usr/bin/python
"""
  (C) Copyright 2018-2019 Intel Corporation.

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
from __future__ import print_function

import traceback
import sys
import os
import re
import time
import yaml
import getpass

# Remove below imports when depricating run_server and stop_server functions.
import subprocess
import json
import resource
import signal
import fcntl
import errno
from avocado.utils import genio
# Remove above imports when depricating run_server and stop_server functions.

from command_utils import BasicParameter, FormattedParameter, ExecutableCommand
from command_utils import ObjectWithParameters, CommandFailure
from command_utils import DaosCommand, Orterun, CommandWithParameters
from general_utils import pcmd, get_file_path
from dmg_utils import storage_format
from write_host_file import write_host_file

SESSIONS = {}

DEFAULT_FILE = "src/tests/ftest/data/daos_server_baseline.yaml"
AVOCADO_FILE = "src/tests/ftest/data/daos_avocado_test.yaml"


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class DaosServer(DaosCommand):
    """Defines an object representing a server command."""

    def __init__(self, path=""):
        """Create a server command object

        Args:
            path (str): path to location of daos_server binary.
        """
        super(DaosServer, self).__init__(
            "/run/daos_server/*", "daos_server", path)

        self.yaml_params = self.DaosServerConfig()
        self.timeout = 30
        self.server_cnt = 1
        self.mode = "normal"

        self.debug = FormattedParameter("-b", True)
        self.json = FormattedParameter("-j", False)
        self.config = FormattedParameter("-o {}")

    def get_params(self, test):
        """Get params for Server object and server configuration."""
        super(DaosServer, self).get_params(test)
        self.yaml_params.get_params(test)

    def get_action_command(self):
        """Set the action command object based on the yaml provided value."""
        # pylint: disable=redefined-variable-type
        if self.action.value == "start":
            self.action_command = self.ServerStartSubCommand()
        else:
            self.action_command = None

    def set_config(self, yamlfile):
        """Set the config value of the parameters in server command."""
        self.config.value = self.yaml_params.create_yaml(yamlfile)
        self.mode = "normal"
        if self.yaml_params.is_nvme() or self.yaml_params.is_scm():
            self.mode = "format"

    def check_subprocess_status(self, sub_process):
        """Wait for message from command output.

        Args:
            sub_process (process.SubProcess): subprocess used to run the command
        """
        patterns = {
            "format": "SCM format required",
            "chowner": "formatting complete and file ownership changed",
            "normal": "DAOS I/O server.*started",
        }
        start_time = time.time()
        start_msgs = 0
        timed_out = False
        while start_msgs != self.server_cnt and not timed_out:
            output = sub_process.get_stdout()
            start_msgs = len(re.findall(patterns[self.mode], output))
            timed_out = time.time() - start_time > self.timeout

        if start_msgs != self.server_cnt:
            err_msg = "{} detected. Only {}/{} messages received".format(
                "Time out" if timed_out else "Error",
                start_msgs, self.server_cnt)
            self.log.info("%s:\n%s", err_msg, sub_process.get_stdout())
            return False

        self.log.info("Started server in <%s> mode in %d seconds", self.mode,
                      time.time() - start_time)
        return True

    class ServerStartSubCommand(CommandWithParameters):
        """Defines an object representing a daos_server start sub command."""

        def __init__(self):
            """Create a start subcommand object."""
            super(DaosServer.ServerStartSubCommand, self).__init__(
                "/run/daos_server/start/*", "start")
            self.port = FormattedParameter("-p {}")
            self.storage = FormattedParameter("-s {}")
            self.modules = FormattedParameter("-m {}")
            self.targets = FormattedParameter("-t {}")
            self.xshelpernr = FormattedParameter("-x {}")
            self.firstcore = FormattedParameter("-f {}")
            self.group = FormattedParameter("-g {}")
            self.attach = FormattedParameter("-a {}")
            self.sock_dir = FormattedParameter("-d {}")
            self.insecure = FormattedParameter("-i", True)

    class DaosServerConfig(ObjectWithParameters):
        """Object to manage the configuration of the server command."""
        # pylint: disable=pylint-no-self-use

        def __init__(self):
            """ Create a DaosServerConfig object."""
            super(DaosServer.DaosServerConfig, self).__init__(
                "/run/server_config/*")
            self.data = None
            self.name = BasicParameter(None, "daos_server")
            self.port = BasicParameter(None, "10001")
            self.nvme = None
            self.scm = None

        @property
        def name(self):
            """Get the name from the server config."""
            if self.data and "name" in self.data:
                return self.data["name"]
            return None

        @name.setter
        def name(self, value):
            """Set the server config name attribute."""
            if self.data and "name" in self.data:
                self.data["name"] = value

        @property
        def port(self):
            """Get the port from the server config."""
            if self.data and "port" in self.data:
                return self.data["port"]
            return None

        @port.setter
        def port(self, value):
            """Set the port config attribute."""
            if self.data and "port" in self.data:
                self.data["port"] = value

        def get_params(self, test):
            """Get values for all of the command params from the yaml file.

            If no key matches are found in the yaml file the BasicParameter
            object will be set to its default value.

            Args:
                test (Test): avocado Test object
            """
            super(DaosServer.DaosServerConfig, self). get_params(test)
            # Read the baseline conf file data/daos_server_baseline.yml
            try:
                with open('{}/{}'.format(test.basepath, DEFAULT_FILE), 'r') \
                as rfile:
                    self.data = yaml.safe_load(rfile)
            except Exception as err:
                self.log.info("<SERVER> Exception occurred: %s", str(err))
                traceback.print_exception(
                    err.__class__, err, sys.exc_info()[2])
                raise ServerFailed(
                    "Failed to Read {}/{}".format(test.basepath, DEFAULT_FILE))

            # Read the values from avocado_testcase.yaml file if test ran
            # with Avocado.
            new_value_set = {}
            if "AVOCADO_TEST_DATADIR" in os.environ:
                avocado_yaml_file = str(
                    os.environ["AVOCADO_TEST_DATADIR"]).split(".")[0] + ".yaml"

                # Read avocado test yaml file.
                try:
                    with open(avocado_yaml_file, 'r') as rfile:
                        filedata = rfile.read()
                    # Remove !mux for yaml load
                    new_value_set = yaml.safe_load(
                        filedata.replace('!mux', ''))
                except Exception as err:
                    self.log.info("<SERVER> Exception occurred: %s", str(err))
                    traceback.print_exception(
                        err.__class__, err, sys.exc_info()[2])
                    raise ServerFailed(
                        "Failed to Read {}".format(
                            '{}.tmp'.format(avocado_yaml_file)))

            # Update values from avocado_testcase.yaml in DAOS yaml variables.
            if new_value_set:
                if 'server' in new_value_set['server_config']:
                    for key in new_value_set['server_config']['server']:
                        self.data['servers'][0][key] = \
                            new_value_set['server_config']['server'][key]
                for key in new_value_set['server_config']:
                    if 'server' not in key:
                        self.data[key] = \
                            new_value_set['server_config'][key]

            # Check if nvme and scm are enabled
            srv_cfg = self.data['servers'][0]
            if 'bdev_class' in srv_cfg and srv_cfg['bdev_class'] == "nvme":
                self.nvme = True
                self.data['user_name'] = getpass.getuser()
            if 'scm_class' in srv_cfg and srv_cfg['scm_class'] == "dcpm":
                self.scm = True
                self.data['user_name'] = getpass.getuser()

            # if specific log file name specified use that
            if hasattr(test, "server_log") and test.server_log is not None:
                self.data['servers'][0]['log_file'] = test.server_log

        def is_nvme(self):
            """Return if NVMe is provided in the configuration."""
            return self.nvme if self.nvme is not None else False

        def is_scm(self):
            """Return if SCM is provided in the configuration."""
            return self.scm if self.scm is not None else False

        def create_yaml(self, yamlfile):
            """Create the DAOS server config YAML file based on Avocado test
                Yaml file.

            Args:
                yamlfile (str): full path and name of yaml file

            Raises:
                ServerFailed: if there is an reading/writing yaml files

            Returns:
                (str): Absolute path of create server yaml file

            """
            # Write self.data dictionary in to AVOCADO_FILE
            # This will be used to start with daos_server -o option.
            self.log.info("Creating the server yaml file: %s", yamlfile)
            try:
                with open(yamlfile, 'w') as wfile:
                    yaml.dump(self.data, wfile, default_flow_style=False)
            except Exception as err:
                self.log.info("<SERVER> Exception occurred:%s", str(err))
                traceback.print_exception(
                    err.__class__, err, sys.exc_info()[2])
                raise ServerFailed("Failed to Write {}".format(yamlfile))

            return os.path.join(yamlfile)


class ServerManager(ExecutableCommand):
    """Defines object to manage server functions and launch server command."""
    # pylint: disable=pylint-no-self-use

    def __init__(self, daosbinpath, runnerpath, attach="/tmp", timeout=300):
        """Create a ServerManager object.

        Args:
            daosbinpath (str): Path to daos bin
            runnerpath (str): Path to Orterun binary.
            attach (str, optional): Defaults to "/tmp".
            timeout (int, optional): Time for the server to start.
                Defaults to 300.
        """
        super(ServerManager, self).__init__("/run/server_manager/*", "", "")

        self.daosbinpath = daosbinpath
        self._hosts = None

        # Setup orterun command defaults
        self.runner = Orterun(
            DaosServer(self.daosbinpath), runnerpath, True)

        # Setup server command defaults
        self.runner.job.action.value = "start"
        self.runner.job.get_action_command()

        # Parameters that user can specify in the test yaml to modify behavior.
        self.debug = BasicParameter(None, True)       # ServerCommand param
        self.attach = BasicParameter(None, attach)    # ServerCommand param
        self.insecure = BasicParameter(None, True)    # ServerCommand param
        self.sudo = BasicParameter(None, False)       # ServerCommand param
        self.srv_timeout = BasicParameter(None, timeout)   # ServerCommand param
        self.report_uri = BasicParameter(None)             # Orterun param
        self.enable_recovery = BasicParameter(None, True)  # Orterun param
        self.export = BasicParameter(None)                 # Orterun param

    @property
    def hosts(self):
        """Hosts attribute getter."""
        return self._hosts

    @hosts.setter
    def hosts(self, value):
        """Hosts attribute setter

        Args:
            value (tuple): (list of hosts, workdir, slots)
        """
        self._hosts, workdir, slots = value
        self.runner.processes.value = len(self._hosts)
        self.runner.hostfile.value = write_host_file(
            self._hosts, workdir, slots)
        self.runner.job.server_cnt = len(self._hosts)

    def get_params(self, test):
        """Get values from the yaml file and assign them respectively
            to the server command and the orterun command.

        Args:
            test (Test): avocado Test object
        """
        server_params = ["debug", "sudo", "srv_timeout"]
        server_start_params = ["attach", "insecure"]
        runner_params = ["enable_recovery", "export", "report_uri"]
        super(ServerManager, self).get_params(test)
        self.runner.job.yaml_params.get_params(test)
        self.runner.get_params(test)
        for name in self.get_param_names():
            if name in server_params:
                if name == "sudo":
                    setattr(self.runner.job, name, getattr(self, name).value)
                elif name == "srv_timeout":
                    setattr(self.runner.job, name, getattr(self, name).value)
                else:
                    getattr(
                        self.runner.job, name).value = getattr(self, name).value
            if name in server_start_params:
                getattr(self.runner.job.action_command, name).value = \
                    getattr(self, name).value
            if name in runner_params:
                getattr(self.runner, name).value = getattr(self, name).value

    def run(self):
        """Execute the runner subprocess."""
        self.log.info("Start CMD>>> %s", str(self.runner))
        return self.runner.run()

    def start(self, yamlfile):
        """Start the server through the runner."""
        self.runner.job.set_config(yamlfile)
        self.server_clean()

        # Prepare nvme storage in servers
        if self.runner.job.yaml_params.is_nvme():
            self.log.info("Performing nvme storage prepare in <format> mode")
            storage_prepare(self._hosts, "root")
            self.runner.job.sudo = True

            # Make sure log file has been created for ownership change
            data_dict = self.runner.job.yaml_params.data
            if data_dict and "log_file" in data_dict['servers'][0]:
                self.log.info("Creating log file")
                c_log = "touch {}".format(data_dict['servers'][0]['log_file'])
                pcmd(self._hosts, c_log, False)

        try:
            self.run()
        except CommandFailure as details:
            self.log.info("<SERVER> Exception occurred: %s", str(details))
            # Kill the subprocess, anything that might have started
            self.kill()
            raise ServerFailed(
                "Failed to start server in {} mode.".format(
                    self.runner.job.mode))

        if self.runner.job.yaml_params.is_nvme() or \
           self.runner.job.yaml_params.is_scm():
            # Setup the hostlist to pass to dmg command
            servers_with_ports = [
                "{}:{}".format(host, self.runner.job.yaml_params.port)
                for host in self._hosts]

            # Format storage and wait for server to change ownership
            self.log.info("Formatting hosts: <%s>", self._hosts)
            storage_format(self.daosbinpath, ",".join(servers_with_ports))

            # Check for server to have changed ownership.
            self.runner.job.mode = "chowner"
            try:
                self.runner.job.check_subprocess_status(self.runner.process)
            except CommandFailure as error:
                self.log.info("Failed to start after format: %s", str(error))

            # Stop the server in case it didn't finish after chowner
            try:
                self.log.info("Stopping servers started as root")
                self.runner.stop()
            except CommandFailure as error:
                raise ServerFailed(
                    "Failed to stop servers after format: {}".format(error))

            # Setup server for run as non-root user
            self.log.info("Starting server as non-root user: <%s>", self._hosts)
            storage_prepare(self._hosts, getpass.getuser())
            self.runner.job.sudo = False
            self.runner.job.mode = "normal"

            # Clean spdk pci lock file
            self.log.info("Cleanup of /tmp/spdk_pci_lock file")
            pcmd(self._hosts, "sudo rm -f /tmp/spdk_pci_lock_0000\\:*", False)

            # Restart server as non root user and check if started
            try:
                self.run()
            except CommandFailure as details:
                self.log.info("<SERVER> Exception occurred: %s", str(details))
                # Kill the subprocess, anything that might have started
                self.kill()
                raise ServerFailed(
                    "Failed to start server in {} mode.".format(
                        self.runner.job.mode))

        return True

    def stop(self):
        """Stop the server through the runner."""
        self.log.info("Stopping servers")
        try:
            self.runner.stop()
            if self.runner.job.yaml_params.is_nvme() or \
               self.runner.job.yaml_params.is_scm():
                storage_reset(self._hosts)
        except CommandFailure as error:
            raise ServerFailed("Failed to stop servers:{}".format(error))

    def server_clean(self):
        """Prepare the hosts before starting daos server."""
        # Kill any doas servers running on the hosts
        self.kill()
        # Clean up any files that exist on the hosts
        self.clean_files()

    def kill(self):
        """Forcably kill any daos server processes running on hosts.

        Sometimes stop doesn't get everything.  Really whack everything
        with this.

        """
        kill_cmds = [
            "sudo pkill '(daos_server|daos_io_server)' --signal INT",
            "sleep 5",
            "pkill '(daos_server|daos_io_server)' --signal KILL",
        ]
        self.log.info("Killing any server processes")
        pcmd(self._hosts, "; ".join(kill_cmds), False, None, None)

    def clean_files(self):
        """Clean the tmpfs on the servers."""
        clean_cmds = [
            "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | xargs -0r rm -rf"
        ]

        if self.runner.job.yaml_params.is_nvme() or \
           self.runner.job.yaml_params.is_scm():
            clean_cmds.append("sudo rm -rf /mnt/daos; sudo umount /mnt/daos")

        self.log.info("Cleanup of /mnt/daos directory.")
        pcmd(self._hosts, "; ".join(clean_cmds), False)


def storage_prepare(hosts, user):
    """
    Prepare the storage on servers using the DAOS server's yaml settings file.
    Args:
        hosts (str): a string of comma-separated host names
    Raises:
        ServerFailed: if server failed to prepare storage
    """
    daos_srv_bin = get_file_path("bin/daos_server")
    cmd = ("sudo {} storage prepare -n -u \"{}\" --hugepages=4096 -f"
           .format(daos_srv_bin[0], user))
    result = pcmd(hosts, cmd, timeout=120)
    if len(result) > 1 or 0 not in result:
        raise ServerFailed("Error preparing NVMe storage")


def storage_reset(hosts):
    """
    Reset the Storage on servers using the DAOS server's yaml settings file.
    Args:
        hosts (str): a string of comma-separated host names
    Raises:
        ServerFailed: if server failed to reset storage
    """
    daos_srv_bin = get_file_path("bin/daos_server")
    cmd = "sudo {} storage prepare -n --reset -f".format(daos_srv_bin[0])
    result = pcmd(hosts, cmd)
    if len(result) > 1 or 0 not in result:
        raise ServerFailed("Error resetting NVMe storage")


SESSIONS = {}


def create_server_yaml(basepath, log_filename):
    """Create the DAOS server config YAML file based on Avocado test Yaml file.

    Args:
        basepath (str): DAOS install basepath
        log_filename (str): log file name
    Raises:
        ServerFailed: if there is an reading/writing yaml files

    """
    # Read the baseline conf file data/daos_server_baseline.yml
    try:
        with open('{}/{}'.format(basepath, DEFAULT_FILE), 'r') as read_file:
            default_value_set = yaml.safe_load(read_file)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed(
            "Failed to Read {}/{}".format(basepath, DEFAULT_FILE))

    # Read the values from avocado_testcase.yaml file if test ran with Avocado.
    new_value_set = {}
    if "AVOCADO_TEST_DATADIR" in os.environ:
        avocado_yaml_file = str(os.environ["AVOCADO_TEST_DATADIR"]).\
                                split(".")[0] + ".yaml"

        # Read avocado test yaml file.
        try:
            with open(avocado_yaml_file, 'r') as rfile:
                filedata = rfile.read()
            # Remove !mux for yaml load
            new_value_set = yaml.safe_load(filedata.replace('!mux', ''))
        except Exception as excpn:
            print("<SERVER> Exception occurred: {0}".format(str(excpn)))
            traceback.print_exception(
                excpn.__class__, excpn, sys.exc_info()[2])
            raise ServerFailed(
                "Failed to Read {}".format('{}.tmp'.format(avocado_yaml_file)))

    # Update values from avocado_testcase.yaml in DAOS yaml variables.
    if new_value_set:
        if 'server' in new_value_set['server_config']:
            for key in new_value_set['server_config']['server']:
                default_value_set['servers'][0][key] = \
                        new_value_set['server_config']['server'][key]
        for key in new_value_set['server_config']:
            if 'server' not in key:
                default_value_set[key] = new_value_set['server_config'][key]

    # if sepcific log file name specified use that
    if log_filename:
        default_value_set['servers'][0]['log_file'] = log_filename

    # Write default_value_set dictionary in to AVOCADO_FILE
    # This will be used to start with daos_server -o option.
    try:
        with open('{}/{}'.format(basepath,
                                 AVOCADO_FILE), 'w') as write_file:
            yaml.dump(default_value_set, write_file, default_flow_style=False)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed("Failed to Write {}/{}".format(basepath,
                                                          AVOCADO_FILE))


def run_server(hostfile, setname, basepath, uri_path=None, env_dict=None,
               clean=True, log_filename=None):
    """Launch DAOS servers in accordance with the supplied hostfile.

    Args:
        hostfile (str): hostfile defining on which hosts to start servers
        setname (str): session name
        basepath (str): DAOS install basepath
        uri_path (str, optional): path to uri file. Defaults to None.
        env_dict (dict, optional): dictionary on env variable names and values.
            Defaults to None.
        clean (bool, optional): clean the mount point. Defaults to True.
        log_filename (str): log file name
    Raises:
        ServerFailed: if there is an error starting the servers

    """
    global SESSIONS    # pylint: disable=global-variable-not-assigned
    try:
        servers = (
            [line.split(' ')[0] for line in genio.read_all_lines(hostfile)])
        server_count = len(servers)

        # Create the DAOS server configuration yaml file to pass
        # with daos_server -o <FILE_NAME>
        print("Creating the server yaml file")
        create_server_yaml(basepath, log_filename)

        # first make sure there are no existing servers running
        print("Removing any existing server processes")
        kill_server(servers)

        # clean the tmpfs on the servers
        if clean:
            print("Cleaning the server tmpfs directories")
            result = pcmd(
                servers,
                "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | "
                "xargs -0r rm -rf",
                verbose=False)
            if len(result) > 1 or 0 not in result:
                raise ServerFailed(
                    "Error cleaning tmpfs on servers: {}".format(
                        ", ".join(
                            [str(result[key]) for key in result if key != 0])))

        # Pile of build time variables
        with open(os.path.join(basepath, ".build_vars.json")) as json_vars:
            build_vars = json.load(json_vars)
        orterun_bin = os.path.join(build_vars["OMPI_PREFIX"], "bin", "orterun")
        daos_srv_bin = os.path.join(build_vars["PREFIX"], "bin", "daos_server")

        server_cmd = [orterun_bin, "--np", str(server_count)]
        if uri_path is not None:
            server_cmd.extend(["--report-uri", uri_path])
        server_cmd.extend(["--hostfile", hostfile, "--enable-recovery"])

        # Add any user supplied environment
        if env_dict is not None:
            for key, value in env_dict.items():
                os.environ[key] = value
                server_cmd.extend(["-x", "{}={}".format(key, value)])

        # the remote orte needs to know where to find daos, in the
        # case that it's not in the system prefix
        # but it should already be in our PATH, so just pass our
        # PATH along to the remote
        if build_vars["PREFIX"] != "/usr":
            server_cmd.extend(["-x", "PATH"])

        # Run server in insecure mode until Certificate tests are in place
        server_cmd.extend([daos_srv_bin,
                           "--debug",
                           "--config", '{}/{}'.format(basepath, AVOCADO_FILE),
                           "start", "-i",
                           "-a", os.path.join(basepath, "install", "tmp")])

        print("Start CMD>>>>{0}".format(' '.join(server_cmd)))

        resource.setrlimit(
            resource.RLIMIT_CORE,
            (resource.RLIM_INFINITY, resource.RLIM_INFINITY))

        SESSIONS[setname] = subprocess.Popen(server_cmd,
                                             stdout=subprocess.PIPE,
                                             stderr=subprocess.PIPE)
        fdesc = SESSIONS[setname].stdout.fileno()
        fstat = fcntl.fcntl(fdesc, fcntl.F_GETFL)
        fcntl.fcntl(fdesc, fcntl.F_SETFL, fstat | os.O_NONBLOCK)
        timeout = 600
        start_time = time.time()
        matches = 0
        pattern = "DAOS I/O server.*started"
        expected_data = "Starting Servers\n"
        while True:
            output = ""
            try:
                output = SESSIONS[setname].stdout.read()
            except IOError as excpn:
                if excpn.errno != errno.EAGAIN:
                    raise ServerFailed("Server didn't start: {}".format(excpn))
                continue
            match = re.findall(pattern, output)
            expected_data += output
            matches += len(match)
            if not output or matches == server_count or \
               time.time() - start_time > timeout:
                print("<SERVER>: {}".format(expected_data))
                if matches != server_count:
                    raise ServerFailed("Server didn't start!")
                break
        print(
            "<SERVER> server started and took {} seconds to start".format(
                time.time() - start_time))

    except Exception as error:
        print("<SERVER> Exception occurred: {0}".format(str(error)))
        traceback.print_exception(error.__class__, error, sys.exc_info()[2])
        # We need to end the session now -- exit the shell
        try:
            SESSIONS[setname].send_signal(signal.SIGINT)
            time.sleep(5)
            # get the stderr
            error = SESSIONS[setname].stderr.read()
            if SESSIONS[setname].poll() is None:
                SESSIONS[setname].kill()
            retcode = SESSIONS[setname].wait()
            print(
                "<SERVER> server start return code: {}\nstderr:\n{}".format(
                    retcode, error))
        except KeyError:
            pass
        raise ServerFailed("Server didn't start!")


def stop_server(setname=None, hosts=None):
    """Stop the daos servers.

    Attempt to initiate an orderly shutdown of all orterun processes it has
    spawned by sending a ctrl-c to the process matching the setname (or all
    processes if no setname is provided).

    If a list of hosts is provided, verify that all daos server processes are
    dead.  Report an error if any processes are found and attempt to forcably
    kill the processes.

    Args:
        setname (str, optional): server group name used to match the session
            used to start the server. Defaults to None.
        hosts (list, optional): list of hosts running the server processes.
            Defaults to None.

    Raises:
        ServerFailed: if there was an error attempting to send a signal to stop
            the processes running the servers or after sending the signal if
            there are processes stiull running.

    """
    global SESSIONS    # pylint: disable=global-variable-not-assigned
    try:
        if setname is None:
            for _key, val in SESSIONS.items():
                val.send_signal(signal.SIGINT)
                time.sleep(5)
                if val.poll() is None:
                    val.kill()
                val.wait()
        else:
            SESSIONS[setname].send_signal(signal.SIGINT)
            time.sleep(5)
            if SESSIONS[setname].poll() is None:
                SESSIONS[setname].kill()
            SESSIONS[setname].wait()
        print("<SERVER> server stopped")

    except Exception as error:
        print("<SERVER> Exception occurred: {0}".format(str(error)))
        raise ServerFailed("Server didn't stop!")

    if not hosts:
        return

    # Make sure the servers actually stopped.  Give them time to stop first
    # pgrep exit status:
    #   0 - One or more processes matched the criteria.
    #   1 - No processes matched.
    #   2 - Syntax error in the command line.
    #   3 - Fatal error: out of memory etc.
    time.sleep(5)
    result = pcmd(
        hosts, "pgrep '(daos_server|daos_io_server)'", False, expect_rc=1)
    if len(result) > 1 or 1 not in result:
        bad_hosts = [
            node for key in result if key != 1 for node in list(result[key])]
        kill_server(bad_hosts)
        raise ServerFailed(
            "DAOS server processes detected after attempted stop on {}".format(
                ", ".join([str(result[key]) for key in result if key != 1])))

    # we can also have orphaned ssh processes that started an orted on a
    # remote node but never get cleaned up when that remote node spontaneiously
    # reboots
    subprocess.call(["pkill", "^ssh$"])


def kill_server(hosts):
    """Forcably kill any daos server processes running on the specified hosts.

    Sometimes stop doesn't get everything.  Really whack everything with this.

    Args:
        hosts (list): list of host names where servers are running
    """
    kill_cmds = [
        "pkill '(daos_server|daos_io_server)' --signal INT",
        "sleep 5",
        "pkill '(daos_server|daos_io_server)' --signal KILL",
    ]
    # Intentionally ignoring the exit status of the command
    pcmd(hosts, "; ".join(kill_cmds), False, None, None)

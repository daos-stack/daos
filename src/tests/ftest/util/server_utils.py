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

AVOCADO_FILE = "daos_avocado_test.yaml"

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

        self.yaml_params = DaosServerConfig()
        self.timeout = 120
        self.server_cnt = 1
        self.server_list = []
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
        access_points = ":".join((self.server_list[0],
                                  str(self.yaml_params.port)))
        self.yaml_params.access_points.value = access_points.split()
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
            self.sock_dir = FormattedParameter("-d {}")
            self.insecure = FormattedParameter("-i", True)
            self.recreate = FormattedParameter("--recreate-superblocks", True)


class DaosServerConfig(ObjectWithParameters):
    """Defines the daos_server configuration yaml parameters."""

    class SingleServerConfig(ObjectWithParameters):
        """Defines the configuration yaml parameters for a single server."""

        def __init__(self):
            """Create a SingleServerConfig object."""
            super(DaosServerConfig.SingleServerConfig, self).__init__(
                "/run/server_config/servers/*")

            # Parameters
            #   targets:                count of VOS targets
            #   first_core:             starting index for targets
            #   nr_xs_helpers:          offload helpers per target
            #   fabric_iface:           map to OFI_INTERFACE=eth0
            #   fabric_iface_port:      map to OFI_PORT=31416
            #   log_mask:               map to D_LOG_MASK env
            #   log_file:               map to D_LOG_FILE env
            #   env_vars:               influences DAOS IO Server behaviour
            #       Add to enable scalable endpoint:
            #           - CRT_CREDIT_EP_CTX=0
            #           - CRT_CTX_SHARE_ADDR=1
            #           - CRT_CTX_NUM=8
            #       nvme options:
            #           - IO_STAT_PERIOD=10
            self.targets = BasicParameter(None, 8)
            self.first_core = BasicParameter(None, 0)
            self.nr_xs_helpers = BasicParameter(None, 2)
            self.fabric_iface = BasicParameter(None, "eth0")
            self.fabric_iface_port = BasicParameter(None, 31416)
            self.log_mask = BasicParameter(None, "DEBUG,RPC=ERR,MEM=ERR")
            self.log_file = BasicParameter(None, "/tmp/server.log")
            self.env_vars = BasicParameter(
                None,
                ["ABT_ENV_MAX_NUM_XSTREAMS=100",
                 "ABT_MAX_NUM_XSTREAMS=100",
                 "DAOS_MD_CAP=1024",
                 "CRT_CTX_SHARE_ADDR=0",
                 "CRT_TIMEOUT=30",
                 "FI_SOCKETS_MAX_CONN_RETRY=1",
                 "FI_SOCKETS_CONN_TIMEOUT=2000"]
            )

            # Storage definition parameters:
            #
            # When scm_class is set to ram, tmpfs will be used to emulate SCM.
            #   scm_mount: /mnt/daos        - map to -s /mnt/daos
            #   scm_class: ram
            #   scm_size: 6                 - size in GB units
            #
            # When scm_class is set to dcpm, scm_list is the list of device
            # paths for AppDirect pmem namespaces (currently only one per
            # server supported).
            #   scm_class: dcpm
            #   scm_list: [/dev/pmem0]
            #
            # If using NVMe SSD (will write /mnt/daos/daos_nvme.conf and start
            # I/O service with -n <path>)
            #   bdev_class: nvme
            #   bdev_list: ["0000:81:00.0"] - generate regular nvme.conf
            #
            # If emulating NVMe SSD with malloc devices
            #   bdev_class: malloc          - map to VOS_BDEV_CLASS=MALLOC
            #   bdev_size: 4                - malloc size of each device in GB.
            #   bdev_number: 1              - generate nvme.conf as follows:
            #       [Malloc]
            #       NumberOfLuns 1
            #       LunSizeInMB 4000
            #
            # If emulating NVMe SSD over kernel block device
            #   bdev_class: kdev            - map to VOS_BDEV_CLASS=AIO
            #   bdev_list: [/dev/sdc]       - generate nvme.conf as follows:
            #       [AIO]
            #       AIO /dev/sdc AIO2
            #
            # If emulating NVMe SSD with backend file
            #   bdev_class: file            - map to VOS_BDEV_CLASS=AIO
            #   bdev_size: 16               - file size in GB. Create file if
            #                                 it does not exist.
            #   bdev_list: [/tmp/daos-bdev] - generate nvme.conf as follows:
            #       [AIO]
            #       AIO /tmp/aiofile AIO1 4096
            self.scm_mount = BasicParameter(None, "/mnt/daos")
            self.scm_class = BasicParameter(None, "ram")
            self.scm_size = BasicParameter(None, 6)
            self.scm_list = BasicParameter(None)
            self.bdev_class = BasicParameter(None)
            self.bdev_list = BasicParameter(None)
            self.bdev_size = BasicParameter(None)
            self.bdev_number = BasicParameter(None)

    def __init__(self):
        """Create a DaosServerConfig object."""
        super(DaosServerConfig, self).__init__("/run/server_config/*")

        # Parameters
        self.name = BasicParameter(None, "daos_server")
        self.access_points = BasicParameter(None)       # e.g. "<host>:<port>"
        self.port = BasicParameter(None, 10001)
        self.provider = BasicParameter(None, "ofi+sockets")
        self.socket_dir = BasicParameter(None)          # /tmp/daos_sockets
        self.nr_hugepages = BasicParameter(None, 4096)
        self.control_log_mask = BasicParameter(None, "DEBUG")
        self.control_log_file = BasicParameter(None, "/tmp/daos_control.log")

        # Used to drop privileges before starting data plane
        # (if started as root to perform hardware provisioning)
        self.user_name = BasicParameter(None)           # e.g. 'daosuser'
        self.group_name = BasicParameter(None)          # e.g. 'daosgroup'

        # Single server config parameters
        self.server_params = [self.SingleServerConfig()]

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        super(DaosServerConfig, self).get_params(test)
        for server_params in self.server_params:
            server_params.get_params(test)

    def update_log_file(self, name, index=0):
        """Update the logfile parameter for the daos server.

        Args:
            name (str): new log file name and path
            index (int, optional): server parameter index to update.
                Defaults to 0.
        """
        self.server_params[index].log_file.update(name, "log_file")

    def is_nvme(self):
        """Return if NVMe is provided in the configuration."""
        if self.server_params[-1].bdev_class.value == "nvme":
            return True
        return False

    def is_scm(self):
        """Return if SCM is provided in the configuration."""
        if self.server_params[-1].scm_class.value == "dcpm":
            return True
        return False

    def create_yaml(self, filename):
        """Create a yaml file from the parameter values.

        Args:
            filename (str): the yaml file to create
        """
        # Convert the parameters into a dictionary to write a yaml file
        yaml_data = {"servers": []}
        for name in self.get_param_names():
            value = getattr(self, name).value
            if value is not None and value is not False:
                yaml_data[name] = getattr(self, name).value
        for index in range(len(self.server_params)):
            yaml_data["servers"].append({})
            for name in self.server_params[index].get_param_names():
                value = getattr(self.server_params[index], name).value
                if value is not None and value is not False:
                    yaml_data["servers"][index][name] = value

        # Write default_value_set dictionary in to AVOCADO_FILE
        # This will be used to start with daos_server -o option.
        try:
            with open(filename, 'w') as write_file:
                yaml.dump(yaml_data, write_file, default_flow_style=False)
        except Exception as error:
            print("<SERVER> Exception occurred: {0}".format(error))
            raise ServerFailed(
                "Error writing daos_server command yaml file {}: {}".format(
                    filename, error))
        return filename


class ServerManager(ExecutableCommand):
    """Defines object to manage server functions and launch server command."""
    # pylint: disable=pylint-no-self-use

    def __init__(self, daosbinpath, runnerpath, timeout=300):
        """Create a ServerManager object.

        Args:
            daosbinpath (str): Path to daos bin
            runnerpath (str): Path to Orterun binary.
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
        self.insecure = BasicParameter(None, True)    # ServerCommand param
        self.recreate = BasicParameter(None, True)    # ServerCommand param
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
        self.runner.job.server_list = self._hosts

    def get_params(self, test):
        """Get values from the yaml file and assign them respectively
            to the server command and the orterun command.

        Args:
            test (Test): avocado Test object
        """
        server_params = ["debug", "sudo", "srv_timeout"]
        server_start_params = ["insecure", "recreate"]
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
            self.runner.mca.value = {"plm_rsh_args": "-l root"}

            # Make sure log file has been created for ownership change
            lfile = self.runner.job.yaml_params.server_params[-1].log_file.value
            if lfile is not None:
                self.log.info("Creating log file")
                cmd_touch_log = "touch {}".format(lfile)
                pcmd(self._hosts, cmd_touch_log, False)

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
            self.runner.job.mode = "normal"
            try:
                self.runner.job.check_subprocess_status(self.runner.process)
            except CommandFailure as error:
                self.log.info("Failed to start after format: %s", str(error))

        return True

    def stop(self):
        """Stop the server through the runner."""
        self.log.info("Stopping servers")
        if self.runner.job.yaml_params.is_nvme():
            self.kill()
            storage_reset(self._hosts)
            # Make sure the mount directory belongs to non-root user
            self.log.info("Changing ownership of mount to non-root user")
            cmd = "sudo chown -R {0}:{0} /mnt/daos*".format(getpass.getuser())
            pcmd(self._hosts, cmd, False)
        else:
            try:
                self.runner.stop()
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


def run_server(test, hostfile, setname, uri_path=None, env_dict=None,
               clean=True):
    """Launch DAOS servers in accordance with the supplied hostfile.

    Args:
        test (Test): avocado Test object
        hostfile (str): hostfile defining on which hosts to start servers
        setname (str): session name
        uri_path (str, optional): path to uri file. Defaults to None.
        env_dict (dict, optional): dictionary on env variable names and values.
            Defaults to None.
        clean (bool, optional): clean the mount point. Defaults to True.

    Raises:
        ServerFailed: if there is an error starting the servers

    """
    global SESSIONS    # pylint: disable=global-variable-not-assigned
    try:
        servers = (
            [line.split(' ')[0] for line in genio.read_all_lines(hostfile)])
        server_count = len(servers)

        # Pile of build time variables
        with open("../../.build_vars.json") as json_vars:
            build_vars = json.load(json_vars)

        # Create the DAOS server configuration yaml file to pass
        # with daos_server -o <FILE_NAME>
        print("Creating the server yaml file in {}".format(test.tmp))
        server_yaml = os.path.join(test.tmp, AVOCADO_FILE)
        server_config = DaosServerConfig()
        server_config.get_params(test)
        access_points = ":".join((servers[0], str(server_config.port)))
        server_config.access_points.value = access_points.split()
        if hasattr(test, "server_log") and test.server_log is not None:
            server_config.update_log_file(test.server_log)
        server_config.create_yaml(server_yaml)

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

        server_cmd = [
            os.path.join(build_vars["OMPI_PREFIX"], "bin", "orterun"),
            "--np", str(server_count)]
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
        server_cmd.extend(
            [os.path.join(build_vars["PREFIX"], "bin", "daos_server"),
             "--debug",
             "--config", server_yaml,
             "start", "-i", "--recreate-superblocks"])

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

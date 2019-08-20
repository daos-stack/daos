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
import time
import subprocess
import json
import re
import resource
import signal
import fcntl
import errno
import yaml

from agent_utils import run_agent
from general_utils import pcmd
from command_utils import CommandWithParameters
from command_utils import BasicParameter, FormattedParameter
from avocado.utils import genio, process
from write_host_file import write_host_file

SESSIONS = {}

DEFAULT_FILE = "src/tests/ftest/data/daos_server_baseline.yaml"
AVOCADO_FILE = "src/tests/ftest/data/daos_avocado_test.yaml"


class ServerFailed(Exception):
    """Server didn't start/stop properly."""


class ServerCommand(CommandWithParameters):
    """Defines a object representing a server command."""

    def __init__(self, hosts, path):
        """Create a server Command object"""
        super(ServerCommand, self).__init__(path, "daos_server")

        self.hosts = hosts
        self.process = None
        self.hostfile = None

        self.request = BasicParameter("{}")
        self.action = BasicParameter("{}")
        self.targets = FormattedParameter("-t {}")
        self.config = FormattedParameter("-o {}")
        self.port = FormattedParameter("-p {}")
        self.storage = FormattedParameter("-s {}")
        self.modules = FormattedParameter("-m {}")
        self.xshelpernr = FormattedParameter("-x {}")
        self.firstcore = FormattedParameter("-f {}")
        self.group = FormattedParameter("-g {}")
        self.attach = FormattedParameter("-a {}")
        self.sock_dir = FormattedParameter("-d {}")
        self.insecure = FormattedParameter("-i", None)

    def prepare(self, path, slots):
        """Prepare the hosts before starting daos server.

        Args:
            path (str): location to write the hostfile
            slots (int): slots per host to use in the hostfile

        Raises:
            ServerFailed: if there is any errors preparing the hosts

        """
        # Kill any doas servers running on the hosts
        kill_server(self.hosts)

        # Clean up any files that exist on the hosts
        clean_server(self.hosts)

        # Ensure the environment for the daos server on each host
        run_agent(path, self.hosts)

        # Create the hostfile
        self.hostfile = write_host_file(self.hosts, path, slots)

    def update_configuration(self, basepath):
        """Update the config parameter with a yaml file.

        Args:
            basepath (str): DAOS install basepath
        """
        self.config.update(create_server_yaml(basepath), "server.config")

    def get_param_names(self):
        """Get a sorted list of daos_server command parameter names."""
        names = self.get_attribute_names(FormattedParameter)
        names.extend(["request", "action"])
        return names

    def get_params(self, test, path="/run/daos_server/*"):
        """Get values for all of the server command params using a yaml file.

        Sets each BasicParameter object's value to the yaml key that matches
        the assigned name of the BasicParameter object in this class. For
        example, the self.block_size.value will be set to the value in the yaml
        file with the key 'block_size'.

        Args:
            test (Test): avocado Test object
            path (str, optional): yaml namespace.
                Defaults to "/run/daos_server/*".

        """
        super(ServerCommand, self).get_params(test, path)

    def get_launch_command(self, manager, uri=None, env=None):
        """Get the process launch command used to run daos_server.

        Args:
            manager (str): mpi job manager command
            uri (str, optional): path to uri file. Defaults to None.
            env (dict, optional): dictionary on env variable names and values.
                Defaults to None.

        Raises:
            ServerFailed: if the specified job manager is unsupported.

        Returns:
            str: daos_sever launch command

        """
        if manager is None:
            # Run locally
            return self.__str__()

        elif manager.endswith("orterun"):
            args = []
            if env:
                assign_env = [
                    "{}={}".format(key, val) for key, val in env.items()]
                args = [
                    "-np {}".format(len(self.hosts)),
                    "-hostfile {}".format(self.hostfile),
                    "--enable-recovery",
                ]
                args.extend(["-x {}".format(assign) for assign in assign_env])
            if uri is not None:
                args.append("--report-uri {}".format(uri))

            return "{} {} {}".format(manager, " ".join(args), self.__str__())
        else:
            raise ServerFailed("Unsupported job manager: {}".format(manager))

    def start(self, manager, verbose=True, env=None, sudo=None, timeout=600):
        """Start the daos server on each specified host.

        Args:
            manager (str): mpi job manager command
            verbose (bool, optional): [description]. Defaults to True.
            env ([type], optional): [description]. Defaults to None.
            timeout (int, optional): [description]. Defaults to 600.

        Raises:
            ServerFailed: if there are issues starting the servers

        """
        if self.process is None:
            # Start the daos server as a subprocess
            kwargs = {
                "cmd": self.get_launch_command(manager),
                "verbose": verbose,
                "allow_output_check": "combined",
                "shell": True,
                "env": env,
                "sudo": sudo,
            }
            self.process = process.SubProcess(**kwargs)
            self.process.start()

            # Wait for 'DAOS I/O server' messages to appear in the daos_server
            # output indicating that the servers have started
            start_time = time.time()
            start_msgs = 0
            timed_out = False
            while start_msgs != len(self.hosts) and not timed_out:
                output = self.process.get_stdout()
                start_msgs = len(re.findall("DAOS I/O server", output))
                timed_out = time.time() - start_time > timeout

            if start_msgs != len(self.hosts):
                err_msg = "{} detected starting {}/{} servers".format(
                    "Timed out" if timed_out else "Error",
                    start_msgs, len(self.hosts))
                print("{}:\n{}".format(err_msg, self.process.get_stdout()))
                raise ServerFailed(err_msg)

    def stop(self):
        """Stop the process running the daos servers.

        Raises:
            ServerFailed: if there are errors stopping the servers

        """
        if self.process is not None:
            signal_list = [
                signal.SIGINT, None, None, None,
                signal.SIGTERM, None, None,
                signal.SIGQUIT, None,
                signal.SIGKILL]
            while self.process.poll() is None and signal_list:
                sig = signal_list.pop(0)
                if sig is not None:
                    self.process.send_signal(sig)
                if signal_list:
                    time.sleep(1)
            if not signal_list:
                raise ServerFailed("Error stopping {}".format(self._command))
            self.process = None


def set_nvme_mode(default_value_set, bdev, enabled=False):
    """Enable/Disable NVMe Mode.

    NVMe is enabled by default in yaml file.So disable it for CI runs.

    Args:
        default_value_set (dict): dictionary of default values
        bdev (str): block device name
        enabled (bool, optional): enable NVMe. Defaults to False.
    """
    if 'bdev_class' in default_value_set['servers'][0]:
        if (default_value_set['servers'][0]['bdev_class'] == bdev and
                not enabled):
            del default_value_set['servers'][0]['bdev_class']
    if enabled:
        default_value_set['servers'][0]['bdev_class'] = bdev


def create_server_yaml(basepath):
    """Create the DAOS server config YAML file based on Avocado test Yaml file.

    Args:
        basepath (str): DAOS install basepath

    Raises:
        ServerFailed: if there is an reading/writing yaml files

    Returns:
        (str): Absolute path of create server yaml file
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
        for key in new_value_set['server_config']:
            if key in default_value_set['servers'][0]:
                default_value_set['servers'][0][key] = \
                    new_value_set['server_config'][key]
            elif key in default_value_set:
                default_value_set[key] = new_value_set['server_config'][key]

    # Disable NVMe from baseline data/daos_server_baseline.yml
    set_nvme_mode(default_value_set, "nvme")

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

    return os.path.join(basepath, AVOCADO_FILE)


def run_server(hostfile, setname, basepath, uri_path=None, env_dict=None,
               clean=True):
    """Launch DAOS servers in accordance with the supplied hostfile.

    Args:
        hostfile (str): hostfile defining on which hosts to start servers
        setname (str): session name
        basepath (str): DAOS install basepath
        uri_path (str, optional): path to uri file. Defaults to None.
        env_dict (dict, optional): dictionary on env variable names and values.
            Defaults to None.
        clean (bool, optional): remove files in /mnt/daos. Defaults to True.

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
        create_server_yaml(basepath)

        # first make sure there are no existing servers running
        print("Removing any existing server processes")
        kill_server(servers)

        # clean the tmpfs on the servers
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

        env_args = []
        # Add any user supplied environment
        if env_dict is not None:
            for key, value in env_dict.items():
                os.environ[key] = value
                env_args.extend(["-x", "{}={}".format(key, value)])
        # the remote orte needs to know where to find daos, in the
        # case that it's not in the system prefix
        # but it should already be in our PATH, so just pass our
        # PATH along to the remote
        if build_vars["PREFIX"] != "/usr":
            env_args.extend(["-x", "PATH"])

        server_cmd = [orterun_bin, "--np", str(server_count)]
        if uri_path is not None:
            server_cmd.extend(["--report-uri", uri_path])
        server_cmd.extend(["--hostfile", hostfile, "--enable-recovery"])
        server_cmd.extend(env_args)

        # Run server in insecure mode until Certificate tests are in place
        server_cmd.extend([daos_srv_bin, "-i",
                           "-a", os.path.join(basepath, "install", "tmp"),
                           "-o", '{}/{}'.format(basepath, AVOCADO_FILE)])

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
        result = 0
        pattern = "DAOS I/O server"
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
            result += len(match)
            if not output or result == server_count or \
               time.time() - start_time > timeout:
                print("<SERVER>: {}".format(expected_data))
                if result != server_count:
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


def clean_server(hosts):
    """Clean the tmpfs  on the servers.

    Args:
        hosts (list): list of host names where servers are running
    """
    cleanup_cmds = [
        "find /mnt/daos -mindepth 1 -maxdepth 1 -print0 | xargs -0r rm -rf"
    ]
    # Intentionally ignoring the exit status of the command
    pcmd(hosts, "; ".join(cleanup_cmds), False, None, None)

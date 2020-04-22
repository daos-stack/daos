#!/usr/bin/python
'''
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
'''
from __future__ import print_function

import os
import time
import subprocess
import getpass
import socket
import errno
import fcntl
import re
import yaml

from general_utils import pcmd, check_file_exists
from command_utils import ObjectWithParameters, BasicParameter


class AgentFailed(Exception):
    """Agent didn't start/stop properly."""


class DaosAgentConfig(ObjectWithParameters):
    """Defines the daos_agent configuration yaml parameters."""

    class AgentSecurityConfig(ObjectWithParameters):
        """Defines the configuration yaml parameters for agent security."""

        def __init__(self):
            """Create a AgentSecurityConfig object."""
            super(DaosAgentConfig.AgentSecurityConfig, self).__init__(
                "/run/agent_config/transport_config/*")
            # transport_config:
            #   allow_insecure: true
            #   ca_cert:        .daos/daosCA.crt
            #   cert:           .daos/daos_agent.crt
            #   key:            .daos/daos_agent.key
            #   server_name:    server
            self.allow_insecure = BasicParameter(None, True)
            self.ca_cert = BasicParameter(None, ".daos/daosCA.crt")
            self.cert = BasicParameter(None, ".daos/daos_agent.crt")
            self.key = BasicParameter(None, ".daos/daos_agent.key")
            self.server_name = BasicParameter(None, "server")

    def __init__(self):
        """Create a DaosAgentConfig object."""
        super(DaosAgentConfig, self).__init__("/run/agent_config/*")

        # DaosAgentConfig Parameters
        #   name: daos
        #   access_points: ['server[0]:10001']
        #   port: 10001
        #   hostlist: ['host1', 'host2']
        #   runtime_dir: /var/run/daos_agent
        #   log_file: /tmp/daos_agent.log
        self.name = BasicParameter(None, "daos_server")
        self.access_points = BasicParameter(None)
        self.port = BasicParameter(None, 10001)
        self.hostlist = BasicParameter(None)
        self.runtime_dir = BasicParameter(None, "/var/run/daos_agent")
        self.log_file = BasicParameter(None, "daos_agent.log")

        # Agent transport_config parameters
        self.transport_params = self.AgentSecurityConfig()

    def get_params(self, test):
        """Get values for all of the command params from the yaml file.

        If no key matches are found in the yaml file the BasicParameter object
        will be set to its default value.

        Args:
            test (Test): avocado Test object
        """
        super(DaosAgentConfig, self).get_params(test)
        self.transport_params.get_params(test)

    def update_log_file(self, name):
        """Update the log file name for the daos agent.

        If the log file name is set to None the log file parameter value will
        not be updated.

        Args:
            name (str): log file name
        """
        if name is not None:
            self.log_file.update(name, "agent_config.log_file")

    def create_yaml(self, filename):
        """Create a yaml file from the parameter values.

        Args:
            filename (str): the yaml file to create
        """
        log_dir = os.environ.get("DAOS_TEST_LOG_DIR", "/tmp")

        # Convert the parameters into a dictionary to write a yaml file
        yaml_data = {"transport_config": []}
        for name in self.get_param_names():
            value = getattr(self, name).value
            if value is not None and value is not False:
                if name.endswith("log_file"):
                    yaml_data[name] = os.path.join(log_dir, value)
                else:
                    yaml_data[name] = value

        # transport_config
        yaml_data["transport_config"] = {}
        for name in self.transport_params.get_param_names():
            value = getattr(self.transport_params, name).value
            if value is not None:
                yaml_data["transport_config"][name] = value

        # Write default_value_set dictionary in to self.tmp
        # This will be used to start with daos_agent -o option.
        print("<AGENT> Agent yaml_data= ", yaml_data)
        try:
            with open(filename, 'w') as write_file:
                yaml.dump(yaml_data, write_file, default_flow_style=False)
        except Exception as error:
            print("<AGENT> Exception occurred: {0}".format(error))
            raise AgentFailed(
                "Error writing daos_agent command yaml file {}: {}".format(
                    filename, error))


def run_agent(test, server_list, client_list=None):
    """Start daos agents on the specified hosts.

    Make sure the environment is setup for the security agent and then
    launches it on the compute nodes.

    This is temporary; presuming the agent will deamonize at somepoint and
    can be started killed more appropriately.

    Args:
        test (Test): provides tmp directory for DAOS repo or installation
        server_list (list): nodes acting as server nodes in the test
        client_list (list, optional): nodes acting as client nodes in the
                    test.
            Defaults to None.

    Raises:
        AgentFailed: if there is an error starting the daos agents

    Returns:
        dict: set of subprocess sessions

    """
    sessions = {}
    user = getpass.getuser()

    # if empty client list, 'test' is effectively client
    client_list = include_local_host(client_list)

    # Create the DAOS Agent configuration yaml file to pass
    # with daos_agent -o <FILE_NAME>
    agent_yaml = os.path.join(test.tmp, "daos_agent.yaml")
    agent_config = DaosAgentConfig()
    agent_config.get_params(test)
    agent_config.hostlist.value = client_list

    access_point = ":".join((server_list[0], str(agent_config.port)))
    agent_config.access_points.value = access_point.split()
    agent_config.update_log_file(getattr(test, "agent_log"))
    agent_config.create_yaml(agent_yaml)

    # Verify the domain socket directory is present and owned by this user
    file_checks = (
        ("Server", server_list, "/var/run/daos_server"),
        ("Client", client_list, "/var/run/daos_agent"),
    )
    for host_type, host_list, directory in file_checks:
        status, nodeset = check_file_exists(host_list, directory, user)
        if not status:
            raise AgentFailed(
                "{}: {} missing directory {} for user {}.".format(
                    nodeset, host_type, directory, user))

    # launch the agent
    export_log_mask = "export D_LOG_MASK=DEBUG,RPC=ERR;"
    export_cmd = export_log_mask + "export DD_MASK=mgmt,io,md,epc,rebuild;"
    daos_agent_bin = os.path.join(test.prefix, "bin", "daos_agent")
    daos_agent_cmd = " ".join((export_cmd, daos_agent_bin, "-o", agent_yaml))
    print("<AGENT> Agent command: ", daos_agent_cmd)

    for client in client_list:
        sessions[client] = subprocess.Popen(
            ["/usr/bin/ssh", client, "-o ConnectTimeout=10",
             "{} -i".format(daos_agent_cmd)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT
        )

    # double check agent launched successfully
    timeout = 15
    started_clients = []
    for client in client_list:
        print("<AGENT> Starting agent on {}".format(client))
        file_desc = sessions[client].stdout.fileno()
        flags = fcntl.fcntl(file_desc, fcntl.F_GETFL)
        fcntl.fcntl(file_desc, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        start_time = time.time()
        pattern = "Using logfile"
        expected_data = ""
        while not sessions[client].poll():
            if time.time() - start_time > timeout:
                print("<AGENT>: {}".format(expected_data))
                raise AgentFailed("DAOS Agent didn't start! Agent reported:\n"
                                  "{}before we gave up waiting for it to "
                                  "start".format(expected_data))
            output = ""
            try:
                output = sessions[client].stdout.read()
            except IOError as excpn:
                if excpn.errno != errno.EAGAIN:
                    raise AgentFailed(
                        "Error in starting daos_agent: {0}".format(str(excpn)))
                time.sleep(1)
                continue
            expected_data += output

            match = re.findall(pattern, output)
            if match:
                print(
                    "<AGENT> agent started on node {} in {} seconds".format(
                        client, time.time() - start_time))
                break

        if sessions[client].returncode is not None:
            print("<AGENT> uh-oh, in agent startup, the ssh that started the "
                  "agent on {} has exited with {}.\nStopping agents on "
                  "{}".format(client, sessions[client].returncode,
                              started_clients))
            # kill the ones we started
            stop_agent(sessions, started_clients)
            raise AgentFailed("Failed to start agent on {}".format(client))

    return sessions


def include_local_host(hosts):
    """Ensure the local host is included in the specified host list.

    Args:
        hosts (list): list of hosts

    Returns:
        list: list of hosts including the local host

    """
    local_host = socket.gethostname().split('.', 1)[0]
    if hosts is None:
        hosts = [local_host]
    elif local_host not in hosts:
        # Take a copy of hosts to avoid modifying-in-place
        hosts = list(hosts)
        hosts.append(local_host)
    return hosts


def stop_agent(sessions, client_list=None):
    """Kill ssh and the agent.

    This is temporary; presuming the agent will deamonize at somepoint and can
    be started and killed more appropriately.

    Args:
        sessions (dict): set of subprocess sessions returned by run_agent()
        client_list (list, optional): lists of hosts running the daos agent.
            Defaults to None.

    Raises:
        AgentFailed: if the daos agents failed to stop

    """
    # if empty client list, 'self' is effectively client
    client_list = include_local_host(client_list)

    # Kill the agents processes
    pcmd(client_list, "pkill daos_agent", False)

    # Kill any processes running in the sessions
    for client in sessions:
        if sessions[client].poll() is None:
            sessions[client].kill()
        sessions[client].wait()

    # Check to make sure all the daos agents are dead
    # pgrep exit status:
    #   0 - One or more processes matched the criteria.
    #   1 - No processes matched.
    #   2 - Syntax error in the command line.
    #   3 - Fatal error: out of memory etc.
    time.sleep(5)
    result = pcmd(client_list, "pgrep 'daos_agent'", False, expect_rc=1)
    if len(result) > 1 or 1 not in result:
        raise AgentFailed(
            "DAOS agent processes detected after attempted stop on {}".format(
                ", ".join([str(result[key]) for key in result if key != 1])))

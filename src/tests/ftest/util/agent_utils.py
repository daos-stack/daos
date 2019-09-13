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
import json
import getpass
import socket
import errno
import fcntl
import re

from general_utils import pcmd, check_file_exists


class AgentFailed(Exception):
    """Agent didn't start/stop properly."""


def run_agent(basepath, server_list, client_list=None):
    """Start daos agents on the specified hosts.

    Make sure the environment is setup for the security agent and then launches
    it on the compute nodes.

    This is temporary; presuming the agent will deamonize at somepoint and
    can be started killed more appropriately.

    Args:
        basepath (str): root directory for DAOS repo or installation
        server_list (list): nodes acting as server nodes in the test
        client_list (list, optional): nodes acting as client nodes in the test.
            Defaults to None.

    Raises:
        AgentFailed: if there is an error starting the daos agents

    Returns:
        dict: set of subprocess sessions

    """
    sessions = {}
    user = getpass.getuser()

    # if empty client list, 'self' is effectively client
    if client_list is None:
        client_list = [socket.gethostname().split('.', 1)[0]]
    elif socket.gethostname().split('.', 1)[0] not in client_list:
        client_list += [socket.gethostname().split('.', 1)[0]]

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
    with open(os.path.join(basepath, ".build_vars.json")) as json_vars:
        build_vars = json.load(json_vars)
    daos_agent_bin = os.path.join(build_vars["PREFIX"], "bin", "daos_agent")

    for client in client_list:
        sessions[client] = subprocess.Popen(
            ["ssh", client, "-o ConnectTimeout=10",
             "{} -i".format(daos_agent_bin)],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT
        )

    # double check agent launched successfully
    timeout = 15
    started_clients = []
    for client in client_list:
        file_desc = sessions[client].stdout.fileno()
        flags = fcntl.fcntl(file_desc, fcntl.F_GETFL)
        fcntl.fcntl(file_desc, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        start_time = time.time()
        pattern = "Using logfile"
        expected_data = ""
        while not sessions[client].poll():
            if time.time() - start_time > timeout:
                print("<AGENT>: {}".format(expected_data))
                raise AgentFailed("DAOS Agent didn't start!  Agent reported:\n"
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
    if client_list is None:
        client_list = [socket.gethostname().split('.', 1)[0]]
    elif socket.gethostname().split('.', 1)[0] not in client_list:
        client_list += [socket.gethostname().split('.', 1)[0]]

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

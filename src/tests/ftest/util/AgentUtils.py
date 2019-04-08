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
import signal
import getpass
import socket
import errno
import fcntl
import re

class AgentFailed(Exception):
    pass

class NodeListType:
    """
    Simple enum to represent all possible node types. To be expanded if
    needed.
    """
    SERVER = 0
    CLIENT = 1

def node_setup_okay(node_list, node_server_type):
    """
    Verify the required environment for each node is present.  For now this
    is defined to mean the domain socket directory is present.  May expand
    this later.

    node_list - The set of DAOS nodes for which to verify environment. Node
                lists are expected to contain exclusively agent or
                exclusively server nodes; they may not be mixed.
    """
    if NodeListType.SERVER:
        socket_dir = "/var/run/daos_server"
    elif NodeListType.CLIENT:
        socket_dir = "/var/run/daos_agent"
    else:
        raise AgentFailed("Unknown node type, exiting")

    okay = True
    failed_node = None
    for node in node_list:
        cmd = "test -d " + socket_dir
        resp = subprocess.call(["ssh", node, cmd])
        if resp != 0:
            okay = False
            failed_node = node
            break
    return okay, failed_node, socket_dir

# pylint: disable=too-many-locals
# Disabling check for this function as in this case, more variables is more
# clear than encapsulating in dict or object
def run_agent(basepath, server_list, client_list=None):
    """
    Makes sure the environment is setup for the security agent and then launches
    it on the compute nodes.

    This is temporary; presuming the agent will deamonize at somepoint and
    can be started killed more appropriately.

    basepath    --root directory for DAOS repo or installation
    client_list --those nodes that are acting as compute nodes in the test
    server_list --those nodes that are acting as server nodes in the test
    """
    sessions = {}

    user = getpass.getuser()

    retcode, node, agent_dir = node_setup_okay(server_list, NodeListType.SERVER)
    if not retcode:
        raise AgentFailed("Server node " + node + " does not have directory "
                          + agent_dir + " set up correctly for user "
                          + user + ".")

    # if empty client list, 'self' is effectively client
    if client_list is None:
        client_list = [socket.gethostname().split('.', 1)[0]]

    retcode, node, agent_dir = node_setup_okay(client_list, NodeListType.CLIENT)
    if not retcode:
        raise AgentFailed("Client node " + node + " does not have directory "
                          + agent_dir + " set up correctly for user "
                          + user + ".")

    # launch the agent
    with open(os.path.join(basepath, ".build_vars.json")) as json_vars:
        build_vars = json.load(json_vars)
    daos_agent_bin = os.path.join(build_vars["PREFIX"], "bin", "daos_agent")

    client = None
    for client in client_list:
        cmd = [
            "ssh",
            client,
            daos_agent_bin
        ]

        p = subprocess.Popen(cmd,
                             stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE)
        sessions[client] = p

    # double check agent launched successfully
    file_desc = sessions[client].stderr.fileno()
    flags = fcntl.fcntl(file_desc, fcntl.F_GETFL)
    fcntl.fcntl(file_desc, fcntl.F_SETFL, flags | os.O_NONBLOCK)
    timeout = 5
    start_time = time.time()
    result = 0
    pattern = "Starting daos_agent"
    expected_data = ""
    while True:
        output = ""
        try:
            output = sessions[client].stderr.read()
        except IOError as excpn:
            if excpn.errno != errno.EAGAIN:
                raise excpn
            continue
        match = re.findall(pattern, output)
        expected_data += output
        result += len(match)
        if not output or time.time() - start_time > timeout:
            print("<AGENT>: {}".format(expected_data))
            raise AgentFailed("DAOS Agent didn't start!")
        break
    print("<AGENT> agent started and took %s seconds to start" % \
            (time.time() - start_time))

    return sessions
# pylint: enable=too-many-locals


def stop_agent(client_list, sessions):
    """
    This should kill ssh and the agent.  This is temporary; presuming the
    agent will deamonize at somepoint and can be started killed more
    appropriately.

    client_list -- kill daos_agent on these hosts
    sessions    -- set of subprocess sessions returned by run_agent()
    """

    # this kills the agent
    for client in client_list:
        cmd = "pkill daos_agent"
        resp = subprocess.call(["ssh", client, cmd])

    for client in sessions:
        if sessions[client].poll() is None:
            sessions[client].kill()
        sessions[client].wait()

    # check to make sure it's dead
    time.sleep(5)
    found_hosts = []
    for host in client_list:
        proc = subprocess.Popen(["ssh", host,
                                 "pgrep 'daos_agent'"],
                                stdout=subprocess.PIPE)
        stdout = proc.communicate()[0]
        resp = proc.wait()
        if resp == 0:
            found_hosts.append(host)

    if found_hosts:
        raise AgentFailed("Attempt to kill daos_agent processes {} on hosts "
                          "{} was unsuccessful"
                          .format(', '.join(stdout.splitlines()), found_hosts))

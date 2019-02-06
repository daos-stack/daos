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

import os
import time
import subprocess
import json
import signal

sessions = {}

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
    for node in node_list:
        cmd = "test -d " + socket_dir
        resp = subprocess.call(["ssh", node, cmd])
        if resp != 0:
            okay = False
            break
    return okay

def run_agent(basepath, client_list, server_list):
    """
    Makes sure the environment is setup for the security agent and then launches
    it on the compute nodes.

    This is temporary; presuming the agent will deamonize at somepoint and
    can be started killed more appropriately.

    basepath    --root directory for DAOS repo or installation
    client_list --those nodes that are acting as compute nodes in the test
    server_list --those nodes that are acting as server nodes in the test

    """
    if not node_setup_okay(client_list, NodeListType.CLIENT):
        raise AgentFailed("A compute node isn't configured properly")

    if not node_setup_okay(server_list, NodeListType.SERVER):
        raise AgentFailed("A server node isn't configured properly")

    with open(os.path.join(basepath, ".build_vars.json")) as json_vars:
        build_vars = json.load(json_vars)
    daos_agent_bin = os.path.join(build_vars["PREFIX"], "bin", "daos_agent")

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


def stop_agent(client_list):
    """
    This should kill ssh and the agent.  This is temporary; presuming the
    agent will deamonize at somepoint and can be started killed more
    appropriately.

    client_list -- kill daos_agent on these hosts
    """

    if len(sessions) == 0:
        return

    # this kills the agent
    for client in client_list:
        cmd = "pkill daos_agent"
        resp = subprocess.call(["ssh", client, cmd])
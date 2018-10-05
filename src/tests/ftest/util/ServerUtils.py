#!/usr/bin/python
'''
  (C) Copyright 2018 Intel Corporation.

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
import re
import time

import aexpect
from avocado.utils import genio

sessions = {}

class ServerFailed(Exception):
    """ Server didn't start/stop properly. """

# a callback function used when there is cmd line I/O, not intended
# to be used outside of this file
def printFunc(thestring):
       print "<SERVER>" + thestring

def runServer(hostfile, setname, basepath, uri_path=None, env_dict=None):
    """
    Launches DAOS servers in accordance with the supplied hostfile.

    """
    global sessions
    try:
        server_count = len(genio.read_all_lines(hostfile))

        # pile of build time variables
        with open(os.path.join(basepath, ".build_vars.json")) as json_vars:
            build_vars = json.load(json_vars)
        orterun_bin = os.path.join(build_vars["OMPI_PREFIX"], "bin/orterun")
        daos_srv_bin = os.path.join(build_vars["PREFIX"], "bin/daos_server")
        ld_lib_path = os.path.join(build_vars["PREFIX"], "lib") + os.pathsep + \
                      os.path.join(build_vars["PREFIX"], "lib/daos_srv")

        # before any set in env are added to env_args, add any user supplied
        # envirables to environment first
        if env_dict is not None:
            for k, v in env_dict.items():
                os.environ[k] = v

        env_vars = ['CRT_.*', 'DAOS_.*', 'ABT_.*', 'DD_(STDERR|LOG)', 'D_LOG_.*',
                    'OFI_.*']

        env_args = ""
        for (env_var, env_val) in os.environ.items():
            for pat in env_vars:
                if re.match(pat, env_var):
                    env_args += "-x {}='{}' ".format(env_var, env_val)

        initial_cmd = "/bin/sh"
        server_cmd = orterun_bin + " --np {0} ".format(server_count)
        if uri_path is not None:
            server_cmd += "--report-uri {0} ".format(uri_path)
        server_cmd += "--hostfile {0} --enable-recovery ".format(hostfile)
        server_cmd += env_args
        server_cmd += "-x DD_SUBSYS=all -x DD_MASK=all "
        server_cmd += "-x LD_LIBRARY_PATH={0} ".format(ld_lib_path)
        server_cmd += daos_srv_bin + " -g {0} -c 1 ".format(setname)
        server_cmd += " -a " + basepath + "/install/tmp/"

        print "Start CMD>>>>{0}".format(server_cmd)

        sessions[setname] = aexpect.ShellSession(initial_cmd)
        if sessions[setname].is_responsive():
            sessions[setname].sendline(server_cmd)
            timeout = 300
            start_time = time.time()
            result = 0
            expected_data = "Starting Servers\n"
            while True:
                pattern = "DAOS server"
                output = sessions[setname].read_nonblocking(2, 2)
                match = re.findall(pattern, output)
                expected_data = expected_data + output
                result += len(match)
                if result == server_count or time.time() - start_time > timeout:
                    print ("<SERVER>: {}".format(expected_data))
                    if result != server_count:
                        raise ServerFailed("Server didn't start!")
                    break
            print "<SERVER> server started and took %s seconds to start" % \
                  (time.time() - start_time)
    except Exception as e:
        print "<SERVER> Exception occurred: {0}".format(str(e))
        raise ServerFailed("Server didn't start!")

def stopServer(setname=None, hosts=None):
    """
    orterun says that if you send a ctrl-c to it, it will
    initiate an orderly shutdown of all the processes it
    has spawned.  Doesn't always work though.
    """

    global sessions
    try:
        if setname == None:
            for k, v in sessions.items():
                v.sendcontrol("c")
                v.sendcontrol("c")
                v.close()
        else:
            sessions[setname].sendcontrol("c")
            sessions[setname].sendcontrol("c")
            sessions[setname].close()
        print "<SERVER> server stopped"
    except Exception as e:
        print "<SERVER> Exception occurred: {0}".format(str(e))
        raise ServerFailed("Server didn't stop!")

    if not hosts:
        return

    # make sure they actually stopped
    found_hosts = []
    for host in hosts:
        resp = subprocess.call("ssh {0} "
                               "pkill no_daos_server --signal 0 || "
                               "pkill no_daos_io_server "
                               "--signal 0".format(host),
                               shell=True)
        if resp == 0:
            # a daos process was found hanging around!
            found_hosts.append(host)

    if found_hosts:
        raise ServerFailed("daos processes found on hosts "
                           "{} after stopServer()".format(found_hosts))

def killServer(hosts):
    """
    Sometimes stop doesn't get everything.  Really whack everything
    with this.

    hosts -- list of host names where servers are running
    """
    kill_cmds = ["pkill daos_server --signal 9",
                 "pkill daos_io_server --signal 9"]
    for host in hosts:
        for cmd in kill_cmds:
            resp = subprocess.call(["ssh", host, cmd])

#!/usr/bin/python
'''
  (C) Copyright 2017 Intel Corporation.

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

import aexpect
from avocado.utils import genio

sessions = {}

class ServerFailed(Exception):
    """ Server didn't start/stop properly. """

# a callback function used when there is cmd line I/O, not intended
# to be used outside of this file
def printFunc(thestring):
       print "<SERVER>" + thestring

def runServer(hostfile, urifile, setname, basepath):

    global sessions
    try:
        server_count = len(genio.read_all_lines(hostfile))

        initial_cmd = "/bin/sh"
        server_cmd = basepath + "/install/bin/orterun --np {0} ".format(server_count)
        server_cmd += "--hostfile {0} --enable-recovery ".format(hostfile)
        server_cmd += "--report-uri {0} -x D_LOG_FILE=".format(urifile)
        server_cmd += basepath + "/install/tmp/daos.log "
        server_cmd += "-x LD_LIBRARY_PATH={0}/install/lib:{0}/install/lib/daos_srv ".format(
            basepath)
        server_cmd += basepath + "/install/bin/daos_server -g {0}".format(setname)

        print "Start CMD>>>>{0}".format(server_cmd)

        sessions[setname] = aexpect.ShellSession(initial_cmd)
        if (sessions[setname].is_responsive()):
            sessions[setname].sendline(server_cmd)
            sessions[setname].read_until_any_line_matches(
                "DAOS server (v0.0.2) started on rank 0*", print_func=printFunc)
            print "<SERVER> server started"
    except Exception as e:
        print "<SERVER> Exception occurred: {0}".format(str(e))
        raise ServerFailed("Server didn't start!")

def stopServer(setname=None):

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

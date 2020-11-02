#!/usr/bin/python

# Copyright (c) 2018-2019 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
# The Government's rights to use, modify, reproduce, release, perform, display,
# or disclose this software are subject to the terms of the Apache License as
# provided in Contract No. 8F-30005.
# Any reproduction of computer software, computer software documentation, or
# portions thereof marked with this legend must also reproduce the markings.
"""
This script runs the rdb tests. From the command line the tests are run with:

server:
orterun -N 1 --report-uri /tmp/urifile -x LD_LIBRARY_PATH
daos_server -o <builddir>/utils/config/examples/daos_server_rdb_tests.yml
start -d ./ -t 1 -m vos,rdb,rsvc,mgmt,rdbt

client:
orterun --ompi-server file:/tmp/urifile <debug_cmds> -np 1 rdbt init
--group=daos_server --uuid <uuid>
orterun --ompi-server file:/tmp/urifile <debug_cmds> -np 1 rdbt test --update
--group=daos_server
orterun --ompi-server file:/tmp/urifile <debug_cmds> -np 1 rdbt test
--group=daos_server
orterun --ompi-server file:/tmp/urifile <debug_cmds> -np 1 rdbt fini
--group=daos_server

Where debug_cmds = -x D_LOG_MASK=DEBUG,RPC=ERR,MEM=ERR -x DD_SUBSYS=all
-x DD_MASK=all

This script automates the process.
"""

import subprocess
import os
import sys
import time
import signal
import shlex
import string

build_root = os.path.join(sys.path[0], "../../../")
sys.path.insert(0, os.path.join(build_root, "utils/sl"))
from build_info import BuildInfo
from env_modules import load_mpi
from distutils.spawn import find_executable

urifile = "/tmp/urifile"
pid_file = "/tmp/" + str(os.getpid()) + "_output"

# To avoid repetition of parts of the oretrun command.
client_prefix = ""
client_suffix = ""

# In case orterun has quit but the daos_server is still running, save the PID.
#daos_server = None

class ServerFailedToStart(Exception):
        pass

class ServerTimedOut(Exception):
        pass

def set_logfile(config, logfile):
    f = open(config, "r+")
    for line in f.readlines():
        string.replace(line,
                       "  log_file: /tmp/server.log",
                       "  log_file: {}".format(logfile))
    f.close()

def start_server(binfo, orterun):
    """
    Start the DAOS server with an orterun command as a child process. We use
    subprocess.Popen since it returns control to the calling process and
    provides access to the polling feature.
    """
    config_file = os.path.join(build_root, "utils", "config", "examples",
                               "daos_server_unittests.yml")
    log_file = os.path.join(binfo.get("PREFIX"),
                            "TESTING",
                            "daos-rdb-test.log")
    set_logfile(config_file, log_file) # set D_LOG_FILE through config file

    print("Starting DAOS server\n")
    cmd = orterun
    cmd += " -N 1 --report-uri {} ".format(urifile)
    cmd += "-x LD_LIBRARY_PATH "
    cmd += binfo.get("PREFIX") + "/bin/daos_server "
    cmd += "--debug --config {} ".format(config_file)
    cmd += "start -d ./ -t 1 -m vos,rdb,rsvc,mgmt,rdbt -i --recreate-superblocks "
    print("Running command:\n{}".format(cmd))
    sys.stdout.flush()

    try:
        p = subprocess.Popen(shlex.split(cmd))
        return p
    except Exception as e:
        raise ServerFailedToStart("Server failed to start:\n{}".format(e))

def run_client(segment_type):
    """
    There are four client segments to be run, init, update, test, and fini.
    The command line varies slightly for each and in some cases there is a
    tail after the suffix.
    """
    tail = ""

    if segment_type == "init":
        uuid = subprocess.check_output(['uuidgen'])
        tail = " --uuid {}".format(uuid)
    elif segment_type == "update":
        segment_type = "test --update"

    cmd = client_prefix + segment_type + client_suffix + tail
    print("Running command:\n{}".format(cmd))
    rc = os.system(cmd)
    if rc:
        raise Exception("command {} failed with return code {}\n".format(
            cmd, rc))
    return 0

def pid_info(output_line):
    """
    Take a line of 'ps -o pid,comm' output and return the PID number and name.
    The line looks something like:
     9108  orterun
        or
    10183  daos_server
    Need both items. Return a tuple (name, pid)
    Note: there could be leading spaces on the pid.
    """
    info = output_line.lstrip().split()
    try:
        return info[1], info[0]
    except Exception as e:
        print("Unable to retrieve PID info from {}".format(output_line))
        return "", None

def find_child(parent_pid, child_name):
    """
    Given a PID and a process name, see if this PID has any children with the
    specified name. If is does, return the child PID. If not, return None.
    ps -o pid,comm --no-headers --ppid <pid> gives output that looks like this:
     41108 orterun
     41519 ps

    """
    child_pid = None
    cmd = ['ps', '-o', 'pid,comm', '--no-headers', '--ppid', str(parent_pid)]

    try:
        res = subprocess.check_output(cmd)
    except subprocess.CalledProcessError:
        # parent_pid has no children
        return None
    except Exception as e:
        print("ps command failed with: {}".format(e))
        return None

    # Get rid of the trailing blank line from subprocess.check_output
    res = [s for s in res.splitlines() if s]
    for line in res:
        try:
            current_name, current_pid = pid_info(line)
        except Exception as e:
            print("Unable to extract pid and process name from {}".format(
                   line))
            continue

        if current_pid is None:
            return None
        if current_name.startswith(child_name):
            # This is the droid, uh, child we're looking for
            return current_pid
        child_pid = find_child(current_pid, child_name)
        if child_pid is not None:
            return child_pid
    return child_pid

def daos_server_pid():
    """
    Find the pid for the daos_server. Start drilling down from the parent
    (current) process until we get output where one line contains
    "daos_io_server" or "daos_server".
    """
    parent_pid = os.getpid()
    return find_child(parent_pid, "daos_")

def cleanup(daos_server):
    """ Perform cleanup operations. Shut down the DAOS server by killing the
    child processes that have been created. If the daos_server process is
    killed, so are the processes for daos_io_server and orterun (theoretically).
    It has been observed on occasion to go zombie until orterun itself is
    killed.
    """
    # Get PID of the daos server
    cmd = "{} signal.SIGKILL".format(daos_server)

    try:
        os.kill(int(daos_server), signal.SIGKILL)
        print("Shut down DAOS server with os.kill({} signal.SIGKILL)".format(
               daos_server))
    except Exception as e:
        if daos_server is None:
            print("No PID was found for the DAOS server")
        elif "No such process" in e:
            print("The daos_server process is no longer available"
                  " and could not be killed.")
        else:
            print("Unable to shut down DAOS server: {}".format(e))

if __name__ == "__main__":
    """
    Start a DAOS server and then run the four stages of the client.
    """
    print("Running rdb tests")
    rc = 0
    binfo = BuildInfo(os.path.join(build_root, ".build_vars.json"));
    debug_cmds = "-x D_LOG_MASK=DEBUG,RPC=ERR,MEM=ERR " + \
                 "-x DD_SUBSYS=all -x DD_MASK=all"
    if not load_mpi('openmpi'):
        raise ServerFailedToStart("No orterun installed")
    orterun = find_executable('orterun')
    if orterun is None:
        raise ServerFailedToStart("No orterun installed")

    try:
        # Server operations
        p = start_server(binfo, orterun)

        counter = 0
        daos_server = daos_server_pid()
        while daos_server is None:
            if counter >= 120:
                raise ServerTimedOut("No DAOS server process detected before "\
                                     "timeout")
            counter += 1
            time.sleep(1)
            daos_server = daos_server_pid()

        # Give daos_io_server some time to get ready.
        time.sleep(10)

        print("DAOS server started")

        # Client operations
        client_prefix = "{} --ompi-server " \
                        "file:{} {} --np 1 rdbt ".format(
                        orterun urifile, debug_cmds)
        client_suffix = " --group=daos_server"
        # orterun is called for the client four times: init, update, test,
        # and fini
        client_segments = ['init', 'update', 'test', 'fini']

        try:
            for segment in client_segments:
                run_client(segment)
            print("SUCCESS\nrbd tests PASSED")
        except Exception as e:
            print("rbd tests FAILED")
            print("{}".format(e))
            rc = 1

    except ServerFailedToStart as e:
        print("ServerFailedToStart: {}".format(e.message))
        print("FAIL")
        rc = 1

    except ServerTimedOut as e:
        print("ServerTimedOut: {}".format(e))
        print("FAIL")
        rc = 1

    finally:
        # Shut down the DAOS server when we are finished.
        try:
            if not p or p.poll() is not None:
                # If the server is dead, something went very wrong
                print("The server is unexpectedly absent.")
                print("FAIL")
                rc = 1
        except NameError:
            rc = 1
        try:
            cleanup(daos_server)
        except NameError:
            # The daos_server was never defined.
            rc = 1

    sys.exit(rc)

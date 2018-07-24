#!/usr/bin/python

# Copyright (c) 2018 Intel Corporation
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

"""
This script runs the rdb tests. From the command line the tests are run with:

server:
orterun -N 1 --enable-recovery --report-uri /tmp/urifile <debug_cmds>
daos_server -c 1 -m vos,rdb,rdbt

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

from multiprocessing import Process
import subprocess
import os
import sys
import time

# If this script is run as part of the Jenkins build using utils/run_tests.sh
# then SL_OMPI_PREFIX is set and sent into this script as an arg. This is for
# running the test manually. Change this definition to your situation.
PREFIX = os.getcwd() + "/install/"

urifile = "/tmp/urifile"
pid_file = "/tmp/" + str(os.getpid()) + "_output"
debug_cmds = "-x D_LOG_MASK=DEBUG,RPC=ERR,MEM=ERR " + \
             "-x DD_SUBSYS=all -x DD_MASK=all"

# To avoid repetition of parts of the oretrun command.
client_prefix = ""
client_suffix = ""

def start_server():
    """
    Start the DAOS server with an orterun command as a child process. If the
    server starts correctly the os.system call never returns. If it does return,
    an non-None exit code is generated (it could be 0). The parent checks this
    exit code before running the client operations.
    """
    cmd = PREFIX + "/bin/orterun -N 1 --report-uri {} ".format(urifile)
    cmd += debug_cmds + " daos_server -c 1 -m vos,rdb,rdbt "
    print("Running command:\n{}".format(cmd))

    # Save the output of the command so we can retrieve the child's PID to kill
    # the DAOS server process at the end of the test.
    cmd += " > " + pid_file

    # If this call returns, then the server was not able to start and the parent
    # will see a not None exit code.
    os.system(cmd)

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

def cleanup():
    """ Perform cleanup operations. Shut down the DAOS server if necessary by
    killing the child processes that have been created. If the daos_io_server
    process is killed, so are the processes for daos_server and orterun. Remove
    the file that logged the child's PID.
    """
    # Shut down the DAOS server
    print("Shutting down DAOS server with kill -9 <PID>")
    # Get child PID
    try:
        fp = open(pid_file, 'r')
        line = fp.readline()
        fp.close()
        if line:
            # The line returned when starting the DAOS server is something like:
            # DAOS server (v0.0.2) process 22343 started on rank 0 (out of 1)
            # with 1 xstream(s)
            child_pid = line.split("process ")[1].split()[0]
            cmd = "kill -9 {}".format(child_pid)
            os.system(cmd)
    except:
        pass
    os.system("/bin/rm {}".format(pid_file))

if __name__ == "__main__":
    """
    Start a DAOS server and then run the four stages of the client.
    """
    print("Running rdb tests")
    rc = 0

    # If there's an arg, it's the path PREFIX
    if len(sys.argv) > 1:
        PREFIX = sys.argv[1]

    # Start the the server.
    print("Starting DAOS server\n")
    sys.stdout.flush()
    p = Process(target=start_server)
    p.start()
    print("Back from start")

    # Give the server time to start up.
    counter = 0
    server_timed_out = False
    while True:
        if os.path.isfile(pid_file):
            break
        if counter >= 120:
            server_timed_out = True
            print("Server timed out")
            break
        counter += 1
        time.sleep(1)

    # If it is anything other than None, even zero, then something has gone
    # wrong and the server has exited. We can't run the client processes.
    if p.exitcode is not None or server_timed_out:
        print("Server unavailable.\nChild process exit code = {}".format(
            p.exitcode))
        print("FAIL")
        rc = 1
    # If the exitcode of the process running the server is None, the process is
    # running and everything should be okay.
    else:
        print("DAOS server started")
        client_prefix = PREFIX + "/bin/orterun --ompi-server " \
                        "file:{} {} --np 1 rdbt ".format(urifile, debug_cmds)
        client_suffix = " --group=daos_server"

        # orterun is called for the client four times: init, update, test, and
        # fini
        client_segments = ['init', 'update', 'test', 'fini']

        try:
            for segment in client_segments:
                run_client(segment)
            print("SUCCESS")
            print("rbd tests PASSED")
        except Exception as e:
            print("rbd tests FAILED")
            print("{}".format(e))
            rc = 1

    cleanup()

    sys.exit(rc)

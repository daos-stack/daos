#!/usr/bin/python
'''
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
'''
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
import getpass
import threading

from SSHConnection import Ssh
from avocado.utils import genio

SESSIONS = {}

DEFAULT_FILE = "src/tests/ftest/data/daos_server_baseline.yml"
AVOCADO_FILE = "src/tests/ftest/data/daos_avocado_test.yml"
SPDK_SETUP_SCRIPT = "/opt/daos/spdk/scripts/setup.sh"

class ServerFailed(Exception):
    """ Server didn't start/stop properly. """

# a callback function used when there is cmd line I/O, not intended
# to be used outside of this file
def printFunc(thestring):
    print("<SERVER>" + thestring)

def set_nvme_mode(default_value_set, bdev, enabled=False):
    """
    Enable/Disable NVMe Mode.
    NVMe is enabled by default in yaml file.So disable it for CI runs.
    Args:
     - default_value_set: Default dictionary value.
     - bdev : Block device name.
     - enabled: Set True/False for enabling NVMe, disabled by default.
    """
    if 'bdev_class' in default_value_set['servers'][0]:
        if (default_value_set['servers'][0]['bdev_class'] == bdev and
                not enabled):
            del default_value_set['servers'][0]['bdev_class']
    if enabled:
        default_value_set['servers'][0]['bdev_class'] = bdev

def create_server_yaml(basepath):
    """
    This function is to create DAOS server configuration YAML file
    based on Avocado test Yaml file.
    Args:
        - basepath = DAOS install basepath
    """
    #Read the baseline conf file data/daos_server_baseline.yml
    try:
        with open('{}/{}'.format(basepath,
                                 DEFAULT_FILE), 'r') as read_file:
            default_value_set = yaml.safe_load(read_file)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed("Failed to Read {}/{}".format(basepath,
                                                         DEFAULT_FILE))

    #Read the values from avocado_testcase.yaml file if test ran with Avocado.
    new_value_set = {}
    if "AVOCADO_TEST_DATADIR" in os.environ:
        avocado_yaml_file = str(os.environ["AVOCADO_TEST_DATADIR"]).\
                                split(".")[0] + ".yaml"

        # Read avocado test yaml file.
        try:
            with open(avocado_yaml_file, 'r') as rfile:
                filedata = rfile.read()
            #Remove !mux for yaml load
            new_value_set = yaml.safe_load(filedata.replace('!mux', ''))
        except Exception as excpn:
            print("<SERVER> Exception occurred: {0}".format(str(excpn)))
            traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
            raise ServerFailed("Failed to Read {}"
                               .format('{}.tmp'.format(avocado_yaml_file)))

    #Update values from avocado_testcase.yaml in DAOS yaml variables.
    if new_value_set:
        for key in new_value_set['server_config']:
            if key in default_value_set['servers'][0]:
                default_value_set['servers'][0][key] = new_value_set\
                ['server_config'][key]
            elif key in default_value_set:
                default_value_set[key] = new_value_set['server_config'][key]

    #Disable NVMe from baseline data/daos_server_baseline.yml
    set_nvme_mode(default_value_set, "nvme")

    #Write default_value_set dictionary in to AVOCADO_FILE
    #This will be used to start with daos_server -o option.
    try:
        with open('{}/{}'.format(basepath,
                                 AVOCADO_FILE), 'w') as write_file:
            yaml.dump(default_value_set, write_file, default_flow_style=False)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed("Failed to Write {}/{}".format(basepath,
                                                          AVOCADO_FILE))


def runServer(hostfile, setname, basepath, uri_path=None, env_dict=None):
    """
    Launches DAOS servers in accordance with the supplied hostfile.

    """
    global SESSIONS
    try:
        servers = [line.split(' ')[0]
                   for line in genio.read_all_lines(hostfile)]
        server_count = len(servers)

        #Create the DAOS server configuration yaml file to pass
        #with daos_server -o <FILE_NAME>
        create_server_yaml(basepath)

        # first make sure there are no existing servers running
        killServer(servers)

        # clean the tmpfs on the servers
        for server in servers:
            subprocess.check_call(['ssh', server,
                                   "find /mnt/daos -mindepth 1 -maxdepth 1 "
                                   "-print0 | xargs -0r rm -rf"])

        # pile of build time variables
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

        server_cmd = [orterun_bin, "--np", str(server_count)]
        if uri_path is not None:
            server_cmd.extend(["--report-uri", uri_path])
        server_cmd.extend(["--hostfile", hostfile, "--enable-recovery"])
        server_cmd.extend(env_args)
        server_cmd.extend([daos_srv_bin,
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
                    raise excpn
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
        print("<SERVER> server started and took %s seconds to start" % \
              (time.time() - start_time))
    except Exception as error:
        print("<SERVER> Exception occurred: {0}".format(str(error)))
        traceback.print_exception(excpn.__class__, error, sys.exc_info()[2])
        # we need to end the session now -- exit the shell
        try:
            SESSIONS[setname].send_signal(signal.SIGINT)
            time.sleep(5)
            # get the stderr
            error = SESSIONS[setname].stderr.read()
            if SESSIONS[setname].poll() is None:
                SESSIONS[setname].kill()
            retcode = SESSIONS[setname].wait()
            print("<SERVER> server start return code: {}\n" \
                  "stderr:\n{}".format(retcode, error))
        except KeyError:
            pass
        raise ServerFailed("Server didn't start!")

def stopServer(setname=None, hosts=None):
    """
    orterun says that if you send a ctrl-c to it, it will
    initiate an orderly shutdown of all the processes it
    has spawned.  Doesn't always work though.
    """

    global SESSIONS
    try:
        if setname is None:
            for _key, value in SESSIONS.items():
                value.send_signal(signal.SIGINT)
                time.sleep(5)
                if value.poll() is None:
                    value.kill()
                value.wait()
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

    # make sure they actually stopped
    # but give them some time to stop first
    time.sleep(5)
    found_hosts = []
    for host in hosts:
        proc = subprocess.Popen(["ssh", host,
                                 "pgrep '(daos_server|daos_io_server)'"],
                                stdout=subprocess.PIPE)
        stdout = proc.communicate()[0]
        resp = proc.wait()
        if resp == 0:
            # a daos process was found hanging around!
            found_hosts.append(host)

    if found_hosts:
        killServer(found_hosts)
        raise ServerFailed("daos processes {} found on hosts "
                           "{} after stopServer() were "
                           "killed".format(', '.join(stdout.splitlines()),
                                           found_hosts))

    # we can also have orphaned ssh processes that started an orted on a
    # remote node but never get cleaned up when that remote node spontaneiously
    # reboots
    subprocess.call(["pkill", "^ssh$"])

def killServer(hosts):
    """
    Sometimes stop doesn't get everything.  Really whack everything
    with this.

    hosts -- list of host names where servers are running
    """
    kill_cmds = ["pkill '(daos_server|daos_io_server)' --signal INT",
                 "sleep 5",
                 "pkill '(daos_server|daos_io_server)' --signal KILL"]
    for host in hosts:
        subprocess.call("ssh {0} \"{1}\"".format(host, '; '.join(kill_cmds)),
                        shell=True)

class Nvme(threading.Thread):
    """
    NVMe thread class for setting/cleaning up NVMe on servers
    """
    def __init__(self, host, operation, debug=False):
        """
        Initialize the remote machine for SSH Connection.
        Args:
            host : Remote machine IP address or hostname.
            no_of_drive (int): Number of drives to be setup default = 0
            nvme_conf_param (): Parameter to be updated in daos_nvme.conf
            debug : To print the command on console for debug purpose.
        return:
            None
        """
        threading.Thread.__init__(self)
        self.machine = host
        self.operation = operation
        self.host = Ssh(host, sudo=True, debug=debug)

    def init_spdk(self, enable):
        """
        Enabled/Disable SPDK on host
        Args:
            enable [True/False]: True will setup the SPDK and False will disable
                                 SPDK on host
        return:
            None
        Raises:
            generic if spdk enabled/disabled failed
        """
        cmd = ("sudo HUGEMEM=4096 " +
               "TARGET_USER=\"{0}\" {1}".format(getpass.getuser(),
                                                SPDK_SETUP_SCRIPT))
        if enable is False:
            cmd = cmd + " reset"
        rccode = self.host.call(cmd)
        if rccode is not 0:
            raise ServerFailed("ERROR Command = {0} RC = {1}".format(cmd,
                                                                     rccode))

    def setup(self):
        """
        Class setup function
        Args:
            None:
        return:
            None
        Raises:
            generic if permission commands fail on server.
        """
        self.host.connect()
        self.init_spdk(enable=True)

        permission_cmd = ["sudo /usr/bin/chmod 777 /dev/hugepages",
                          "sudo /usr/bin/chmod 666 /dev/uio*",
                          "sudo /usr/bin/chmod 666 \
                          /sys/class/uio/uio*/device/config",
                          "sudo /usr/bin/chmod 666 \
                          /sys/class/uio/uio*/device/resource*",
                          "sudo /usr/bin/rm -f /dev/hugepages/*"]

        rccode = self.host.call("&&".join(permission_cmd))
        if rccode is not 0:
            raise ServerFailed("ERROR Command = {0} RC = {1}"
                               .format(permission_cmd, rccode))
        self.host.disconnect()

    def cleanup(self):
        """
        Class cleanup function
        Args:
            None:
        return:
            None
        Raises:
            generic if cleanup (rm) commands fail on server.
        """
        self.host.connect()
        self.init_spdk(enable=False)
        cmd = "sudo /usr/bin/rm -f /dev/hugepages/*"
        rccode = self.host.call(cmd)
        if rccode is not 0:
            raise ServerFailed("ERROR Command = {0} RC = {1}".format(cmd,
                                                                     rccode))
        self.host.disconnect()

    def run(self):
        """
        Thread Run method for setup or cleanup
        """
        if self.operation == "setup":
            self.setup()
        elif self.operation == "cleanup":
            self.cleanup()

def nvme_setup(hostlist):
    """
    nvme_setup function called from Avocado test
    Args:
        hostlist[list]: servers to start with NVMe.
        nvme_no_of_drive: Total number of drives per server.
        nvme_conf_param: NVMe config file parameters.

        *IMPORTANT* (All above parameter can be passed via yaml file)
        please look at the example Nvme.yaml
    return:
        None
    """
    print("NVMe server Setup Started......")
    host_nvme = []
    for _hosts in hostlist:
        host_nvme.append(Nvme(_hosts,
                              "setup",
                              debug=True))
    for setup_thread in host_nvme:
        setup_thread.start()
    for setup_thread in host_nvme:
        setup_thread.join()
    print("NVMe server Setup Finished......")

def nvme_cleanup(hostlist):
    """
    nvme_cleanup function called from Avocado test
    Args:
        hostlist[list]: Number of hosts to start ther server with NVMe
    return:
        None
    """
    print("NVMe server cleanup Started......")
    host_nvme = []
    for _hosts in hostlist:
        host_nvme.append(Nvme(_hosts,
                              "cleanup",
                              debug=True))
    for cleanup_thread in host_nvme:
        cleanup_thread.start()
    for cleanup_thread in host_nvme:
        cleanup_thread.join()
    print("NVMe server cleanup Finished......")


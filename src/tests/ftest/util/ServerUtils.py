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
import getpass
import aexpect
from SSHConnection import Ssh
import yaml

from avocado.utils import genio

DAOS_PATH = os.environ['DAOS_PATH']
SPDK_SETUP_SCRIPT = "/opt/daos/spdk/scripts/setup.sh"
DAOS_CONF_FILE = "/etc/daos_nvme.conf"

sessions = {}

DEFAULT_YAML_FILE = "src/tests/ftest/data/daos_server_baseline.yml"
AVOCADO_YAML_FILE = "src/tests/ftest/data/daos_avocado_test.yml"

class ServerFailed(Exception):
    """ Server didn't start/stop properly. """

# a callback function used when there is cmd line I/O, not intended
# to be used outside of this file
def printFunc(thestring):
    print("<SERVER>" + thestring)

def nvme_yaml_config(default_value, bdev, enabled=False):
    """
    Enable/Disable NVMe Mode.
    By default it's Enabled in yaml file so disable to run with ofi+sockets.
    """
    if 'bdev_class' in default_value['servers'][0]:
        if (default_value['servers'][0]['bdev_class'] == bdev and
                not enabled):
            del default_value['servers'][0]['bdev_class']
    if enabled:
        default_value['servers'][0]['bdev_class'] = bdev

def remove_mux_from_yaml(avocado_yaml_file, avocado_yaml_file_tmp):
    """
    Function to remove "!mux" from yaml file and create new tmp file
    Args:
        avocado_yaml_file: Avocado test yaml file
        avocado_yaml_file_tmp: Avocado temporary test yaml file
    """
    with open(avocado_yaml_file, 'r') as rfile:
        filedata = rfile.read()
    filedata = filedata.replace('!mux', '')
    with open(avocado_yaml_file_tmp, 'w') as wfile:
        wfile.write(filedata)

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
                                 DEFAULT_YAML_FILE), 'r') as read_file:
            default_value = yaml.safe_load(read_file)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed("Failed to Read {}/{}".format(basepath,
                                                         DEFAULT_YAML_FILE))

    #Read the values from avocado_testcase.yaml file if test ran with Avocado.
    avocado_yaml_value = ""
    if "AVOCADO_TEST_DATADIR" in os.environ:
        avocado_yaml_file = str(os.environ["AVOCADO_TEST_DATADIR"]).\
                                split(".")[0] + ".yaml"
        avocado_yaml_file_tmp = '{}.tmp'.format(avocado_yaml_file)

        # Yaml Python module is not able to read !mux so need to
        # remove !mux before reading.
        remove_mux_from_yaml(avocado_yaml_file, avocado_yaml_file_tmp)

        # Read avocado test yaml file.
        try:
            with open('{}'.format(avocado_yaml_file_tmp), 'r') as read_file:
                avocado_yaml_value = yaml.safe_load(read_file)
        except Exception as excpn:
            print("<SERVER> Exception occurred: {0}".format(str(excpn)))
            traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
            raise ServerFailed("Failed to Read {}"
                               .format('{}.tmp'.format(avocado_yaml_file)))
        #Remove temporary file
        os.remove(avocado_yaml_file_tmp)
    #Update values from avocado_testcase.yaml in DAOS yaml variables.
    for key in avocado_yaml_value['server_config']:
        if key in default_value['servers'][0]:
            default_value['servers'][0][key] = avocado_yaml_value\
            ['server_config'][key]
        elif key in default_value:
            default_value[key] = avocado_yaml_value['server_config'][key]

    #Disable NVMe from baseline data/daos_server_baseline.yml
    nvme_yaml_config(default_value, "nvme")

    #Write default_value dictionary in to AVOCADO_YAML_FILE
    #This will be used to start with daos_server -o option.
    try:
        with open('{}/{}'.format(basepath,
                                 AVOCADO_YAML_FILE), 'w') as write_file:
            yaml.dump(default_value, write_file, default_flow_style=False)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed("Failed to Write {}/{}".format(basepath,
                                                          AVOCADO_YAML_FILE))

def runServer(hostfile, setname, basepath, uri_path=None, env_dict=None):
    """
    Launches DAOS servers in accordance with the supplied hostfile.

    """
    global sessions
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
            for k, v in env_dict.items():
                os.environ[k] = v
                env_args.extend(["-x", "{}={}".format(k, v)])

        server_cmd = [orterun_bin, "--np", str(server_count)]
        if uri_path is not None:
            server_cmd.extend(["--report-uri", uri_path])
        server_cmd.extend(["--hostfile", hostfile, "--enable-recovery"])
        server_cmd.extend(env_args)
        server_cmd.extend([daos_srv_bin,
                           "-a", os.path.join(basepath, "install", "tmp"),
                           "-o", '{}/{}'.format(basepath, AVOCADO_YAML_FILE)])
        print("Start CMD>>>>{0}".format(' '.join(server_cmd)))

        resource.setrlimit(
            resource.RLIMIT_CORE,
            (resource.RLIM_INFINITY, resource.RLIM_INFINITY))

        sessions[setname] = subprocess.Popen(server_cmd,
                                             stdout=subprocess.PIPE,
                                             stderr=subprocess.PIPE)
        fd = sessions[setname].stdout.fileno()
        fl = fcntl.fcntl(fd, fcntl.F_GETFL)
        fcntl.fcntl(fd, fcntl.F_SETFL, fl | os.O_NONBLOCK)
        timeout = 600
        start_time = time.time()
        result = 0
        pattern = "DAOS I/O server"
        expected_data = "Starting Servers\n"
        while True:
            output = ""
            try:
                output = sessions[setname].stdout.read()
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
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        # we need to end the session now -- exit the shell
        try:
            sessions[setname].send_signal(signal.SIGINT)
            time.sleep(5)
            # get the stderr
            error = sessions[setname].stderr.read()
            if sessions[setname].poll() is None:
                sessions[setname].kill()
            retcode = sessions[setname].wait()
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

    global sessions
    try:
        if setname == None:
            for k, v in sessions.items():
                v.send_signal(signal.SIGINT)
                time.sleep(5)
                if v.poll() == None:
                    v.kill()
                v.wait()
        else:
            sessions[setname].send_signal(signal.SIGINT)
            time.sleep(5)
            if sessions[setname].poll() == None:
                sessions[setname].kill()
            sessions[setname].wait()
        print("<SERVER> server stopped")
    except Exception as e:
        print("<SERVER> Exception occurred: {0}".format(str(e)))
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
        subprocess.call("ssh {0} \"{1}\"".format(host, '; '.join(kill_cmds)), shell=True)

class Nvme(object):
    """
    This is the NVMe class
    """
    def __init__(self, host, debug=False):
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
        self.machine = host
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

        rc = self.host.call(cmd)
        if rc is not 0:
            raise ServerFailed("ERROR Command = {0} RC = {1}".format(cmd, rc))

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

        permission_cmd = ["sudo chmod 777 /dev/hugepages",
                          "sudo chmod 666 /dev/uio*",
                          "sudo chmod 666 /sys/class/uio/uio*/device/config",
                          "sudo chmod 666 /sys/class/uio/uio*/device/resource*",
                          "sudo rm -f /dev/hugepages/*"]

        rc = self.host.call("&&".join(permission_cmd))
        if rc is not 0:
            raise ServerFailed("ERROR Command = {0} RC = {1}"
                               .format(permission_cmd, rc))
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
        cmd = ["rm -f /dev/hugepages/*"]

        rc = self.host.call(cmd)
        if rc is not 0:
            raise ServerFailed("ERROR Command = {0} RC = {1}".format(cmds, rc))
        self.host.disconnect()

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
                              debug=True))

    for host in host_nvme:
        host.setup()
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
                              debug=True))

    for host in host_nvme:
        host.cleanup()
    print("NVMe server cleanup Finished......")

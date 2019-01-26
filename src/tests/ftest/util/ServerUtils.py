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
import yaml

from avocado.utils import genio

sessions = {}

DEFAULT_YAML_FILE = "utils/config/examples/daos_server_sockets.yml"
AVOCADO_YAML_FILE = "utils/config/daos_avocado_test.yml"

class ServerFailed(Exception):
    """ Server didn't start/stop properly. """

# a callback function used when there is cmd line I/O, not intended
# to be used outside of this file
def printFunc(thestring):
    print("<SERVER>" + thestring)

def nvme_yaml_config(default_yaml_value, bdev, enabled=False):
    """
    Enable/Disable NVMe Mode.
    By default it's Enabled in yaml file so disable to run with ofi+sockets.
    """
    if 'bdev_class' in default_yaml_value['servers'][0]:
        if default_yaml_value['servers'][0]['bdev_class'] == bdev and not enabled:
            del default_yaml_value['servers'][0]['bdev_class']
    if enabled:
        default_yaml_value['servers'][0]['bdev_class'] = bdev

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

def update_server_options(avocado_yaml_value, default_yaml_value):
    """
    Update the 'server_options' from avocado_yaml_value dictionary to default_yaml_value dictionary.
    """
    default_yaml_env = default_yaml_value["servers"][0]['env_vars']

    if 'server_options' in avocado_yaml_value:
        avocado_yaml_options = avocado_yaml_value['server_options']
        #Iterate through the server_options value from test file
        for key, val in avocado_yaml_options.iteritems():
            if 'env_vars' in key:
                for no_of_env in enumerate(val):
                    for env_k, env_v in no_of_env[1].iteritems():
                        env_index = [default_yaml_env.index(i)
                                     for i in  default_yaml_env if env_k in i]
                        #Update the Existing environment variable
                        if env_index:
                            default_yaml_env[env_index[0]] = "{}={}".format(env_k, env_v)
                        #Add new environment variable
                        else:
                            default_yaml_env.append("{}={}".format(env_k, env_v))
            #Update/Add other non env_vars variable
            else:
                default_yaml_value["servers"][0].update({key:val})

def create_server_yaml(test_cls):
    """
    This function is to create DAOS server configuration YAML file based on Avocado test Yaml file.
    Args:
        - test_cls = Pass Avocado self (Test) class
    """
    #Read the default utils/config/examples/daos_server_sockets.yml
    try:
        with open('{}/{}'.format(test_cls.basepath, DEFAULT_YAML_FILE), 'r') as read_file:
            default_yaml_value = yaml.load(read_file)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed("Failed to Read {}/{}".format(test_cls.basepath, DEFAULT_YAML_FILE))

    #Read the values from avocado_testcase.yaml file
    avocado_yaml_file = str(test_cls.name).split("-")[1].split(":")[0].replace(".py", ".yaml")
    avocado_yaml_file_tmp = '{}.tmp'.format(avocado_yaml_file)

    # Yaml Python module is not able to read !mux so need to remove !mux before reading.
    remove_mux_from_yaml(avocado_yaml_file, avocado_yaml_file_tmp)

    # Read avocado avocado_testcase.yaml file.
    try:
        with open('{}'.format(avocado_yaml_file_tmp), 'r') as read_file:
            avocado_yaml_value = yaml.load(read_file)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed("Failed to Read {}".format('{}.tmp'.format(avocado_yaml_file)))
    #Remove temporary file
    os.remove(avocado_yaml_file_tmp)

    #Update main values from avocado_testcase.yaml in DAOS yaml variables.
    if 'server' in avocado_yaml_value:
        default_yaml_value.update(avocado_yaml_value["server"])

    #Disable NVMe as it's Enabled in utils/config/examples/daos_server_sockets.yml
    nvme_yaml_config(default_yaml_value, "nvme")

    # Update servers: instance value in DAOS yaml
    update_server_options(avocado_yaml_value, default_yaml_value)

    #Write default_yaml_value dictionary in to AVOCADO_YAML_FILE, This will be used to start
    #with daos_server -o option.
    try:
        with open('{}/{}'.format(test_cls.basepath, AVOCADO_YAML_FILE), 'w') as write_file:
            yaml.dump(default_yaml_value, write_file, default_flow_style=False)
    except Exception as excpn:
        print("<SERVER> Exception occurred: {0}".format(str(excpn)))
        traceback.print_exception(excpn.__class__, excpn, sys.exc_info()[2])
        raise ServerFailed("Failed to Write {}/{}".format(test_cls.basepath, AVOCADO_YAML_FILE))

def runServer(test_cls, setname, uri_path=None, env_dict=None):
    """
    Launches DAOS servers in accordance with the supplied hostfile.
    Args:
        - test_cls = Avocado self (Test) class
            - test_cls.basepath
            - test_cls.hostfile
          Example ServerUtils.runServer(self, self.server_group)
        - setname = Server Group name
        - uri_path = uri path
    """
    global sessions

    try:
        servers = [line.split(' ')[0] for line in genio.read_all_lines(test_cls.hostfile)]
        server_count = len(servers)

        #Create the DAOS server configuration yaml file to pass with daos_server -o <FILE_NAME>
        create_server_yaml(test_cls)

        # first make sure there are no existing servers running
        killServer(servers)

        # pile of build time variables
        with open(os.path.join(test_cls.basepath, ".build_vars.json")) as json_vars:
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
        server_cmd.extend(["--hostfile", test_cls.hostfile, "--enable-recovery"])
        server_cmd.extend(env_args)
        server_cmd.extend([daos_srv_bin,
                           "-a", os.path.join(test_cls.basepath, "install", "tmp"),
                           "-o", '{}/{}'.format(test_cls.basepath, AVOCADO_YAML_FILE)])

        print("Start CMD>>>>{0}".format(' '.join(server_cmd)))

        resource.setrlimit(resource.RLIMIT_CORE,
                           (resource.RLIM_INFINITY,
                            resource.RLIM_INFINITY))

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
            if not output or result == server_count or time.time() - start_time > timeout:
                print("<SERVER>: {}".format(expected_data))
                if result != server_count:
                    raise ServerFailed("Server didn't start!")
                break
        print("<SERVER> server started and took %s seconds to start" %(time.time() - start_time))
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
            print("<SERVER> server start return code: {}\n stderr:\n{}".format(retcode, error))
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
        raise ServerFailed("daos processes {} found on hosts {} after stopServer() were killed"
                           .format(', '.join(stdout.splitlines()), found_hosts))

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

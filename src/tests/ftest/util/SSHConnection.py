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
<<<<<<< HEAD
from paramiko import client
=======
from paramiko import client, Transport, SFTPClient
>>>>>>> 52cd77d506a273b148a0d5d94ac19178388bfcec

class Ssh(object):
    """
    It's mimic to the subprocess class, only difference is to use ssh protocol
    and get command execution on remote machine where in subprocess command will
    be initiated on local machine.
    Right now check_output() and call() function implemented.In future more
    methods can be added which will behave like subprocess methods.
    """
    def __init__(self, address, timeout=60, sudo=False, debug=False):
        """
        Initialize the remote machines and open SSH Connection.
        Args:
            address: remote machine IP address or hostname.
            timeout: Timeout for waiting to finish the command
            debug  : To print the command on console.
        return:
            None
        """
        self.address = address
        self.timeout = timeout
        self.sudo = sudo
        self.debug = debug
        self.client = client.SSHClient()
        self.client.set_missing_host_key_policy(client.AutoAddPolicy())

    def connect(self):
        """
        Connect the client via ssh connection
        """
        self.client.connect(self.address)

        if self.debug:
            print("SSH Connection open to {0}.".format(self.address))

    def check_output(self, command):
        """
        Execute command on remote machine.
        Args:
            command: Command to run on remote machine.
        return:
            stdout: stdout of the return code
        Raises:
            general: if it's failed in ssh channel setup, exec command, close()
            general: If command exit status is not 0.
        """
        try:
            transport = self.client.get_transport()
            transport.set_keepalive(20)
            channel = transport.open_session()
            channel.settimeout(self.timeout)
            channel.exec_command(command)
            stdout = channel.makefile('rb').read()
            exit_status = channel.recv_exit_status()
            channel.close()
        except:
            print("Exception in call for command {0}".format(command))
            raise

        if self.debug:
            stderr = channel.makefile_stderr('rb').read()
            print("[{0}] Command :{1}\nRC:{2} \nstdout : {3}\nstderr :{4}"
                  .format(self.address,
                          command,
                          exit_status,
                          stdout,
                          stderr))

        if exit_status != 0:
            raise Exception("Command '{0}' returned non-zero exit status {1}"
                            .format(command, exit_status))

        return stdout

    def call(self, command):
        """
        Execute command on remote machine.
        Args:
            command: Command to run on remote machine.
        return:
            exit_status: int value of exit status
        Raises:
            general: if it's failed in ssh channel setup, exec command, close()
        """
        try:
            transport = self.client.get_transport()
            transport.set_keepalive(20)
            channel = transport.open_session()
            channel.settimeout(self.timeout)
            channel.exec_command(command)
            exit_status = channel.recv_exit_status()
            channel.close()
        except:
            print("Exception in call for command {0}".format(command))
            raise

        if self.debug:
            print("[{0}] Command :{1}\nRC:{2} \n"
                  .format(self.address,
                          command,
                          exit_status))
        return exit_status

    def disconnect(self):
        """
        Disconnect the client ssh connection
        """
        if self.client:
            self.client.close()

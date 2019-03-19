#!/usr/bin/env python3
# Copyright (c) 2016-2017 Intel Corporation
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
# -*- coding: utf-8 -*-
"""
Iofrunner class
The  Iofrunner is triggered by the TestRunner to:
Create the CNSS and IONSS dirs
Trigger the CNSS and IONSS process
and post test run to:
Terminate the CNSS and IONSS processes
Unmount and remove the CNSS and IONSS dirs
"""

import os
import subprocess
import logging
import getpass
import tempfile
import yaml
import socket

#pylint: disable=broad-except
#pylint: disable=too-many-locals

class IofRunner():
    """Simple test runner"""

    def __init__(self, dir_path, test_info, node_control):
        self.dir_path = dir_path
        self.test_info = test_info
        self.node_control = node_control
        self.logger = logging.getLogger("TestRunnerLogger")
        self.use_fs_list = []
        self.proc = None

    def create_cnss_dir(self):
        """Create the CNSS dir on all CNs"""
        procrtn = None
        testmsg = "Create the CNSS dir on all CNs"
        export_tmp_dir = os.getenv("IOF_TMP_DIR", "/tmp")
        tempdir = tempfile.mkdtemp(prefix="tmp_iof_runner",
                                   dir=export_tmp_dir)
        self.test_info.set_passToConfig('CNSS_PREFIX', tempdir)
        os.environ['CNSS_PREFIX'] = tempdir
        cmdstr = "mkdir -p %s " % tempdir
        procrtn = self.node_control.execute_remote_cmd(cmdstr, self.dir_path,
                                                       'IOF_TEST_CN', testmsg)
        if procrtn:
            self.logger.error(
                "TestIOF: Failed to create the CNSS dirs. \
                 Error code: %d\n", procrtn)

    def manage_ionss_dir(self):
        """Create dirs for IONSS backend"""
        procrtn = None
        testmsg = "create dirs for IONSS backend"
        export_tmp_dir = os.getenv("IOF_TMP_DIR", "/tmp")
        ion_dir = tempfile.mkdtemp(prefix="tmp_iof_ionss_",
                                   dir=export_tmp_dir)
        self.test_info.set_passToConfig('ION_TEMPDIR', ion_dir)
        os.environ['ION_TEMPDIR'] = ion_dir
        cmdstr = "mkdir -p %s " % ion_dir
        procrtn = self.node_control.execute_remote_cmd(cmdstr, self.dir_path,
                                                       'IOF_TEST_ION', testmsg)
        if procrtn:
            self.logger.error(
                "TestIOF: Failed to create the IONSS dirs. Error code: %d\n",
                procrtn)
        self.use_fs_list = \
            self.test_info.get_test_info(keyname='fsList',
                                         default=['exp', 'FS_2', 'FS_1'])
        config = {"projections": []}
        for fs in self.use_fs_list:
            abs_path = os.path.join(ion_dir, fs)
            config["projections"].append({"full_path": abs_path})
            testmsg = "creating dirs to be used as Filesystem backend"
            cmdstr = "mkdir -p %s" % abs_path
            procrtn = self.node_control.execute_remote_cmd(cmdstr,
                                                           self.dir_path,
                                                           'IOF_TEST_ION',
                                                           testmsg)
            if procrtn:
                self.logger.error(
                    """"TestIOF: Failed to create dirs for Filesystem
                     backend %s. Error code: %d\n""", fs, procrtn)
        config_file = tempfile.NamedTemporaryFile(suffix='.cfg',
                                                  prefix="ionss_",
                                                  dir=ion_dir, mode='w',
                                                  delete=False)
        self.test_info.set_passToConfig('ION_CONFIG', config_file.name)
        yaml.dump(config, config_file.file, default_flow_style=False)
        config_file.close()
        testmsg = "Creating IONSS config files"
        host = socket.getfqdn().split(".")[0]
        cmdstr = "scp %s:%s %s" % (host, config_file.name, config_file.name)
        procrtn = self.node_control.execute_remote_cmd(cmdstr, self.dir_path,
                                                       'IOF_TEST_ION', testmsg)
        if procrtn:
            self.logger.error(
                "TestIOF: Failed to create the IONSS config. Error code: %d\n",
                procrtn)

    def _add_prefix_logdir(self):
        """Add the log directory to the prefix"""
        ompi_bin = self.test_info.get_defaultENV('IOF_OMPI_BIN')
        log_path = os.path.join(self.dir_path, "ionss")
        os.makedirs(log_path, exist_ok=True)

        cmd = [os.path.join(ompi_bin, 'orterun'),
               '--mca', 'btl', 'self,tcp',
               '--output-filename', log_path]
        if getpass.getuser() == "root":
            cmd.append('--allow-run-as-root')

        dvm_file = self.test_info.get_defaultENV('TR_USE_URI')
        if dvm_file:
            cmd.extend(['--ompi-server',
                        'file:{}'.format(dvm_file)])
        return cmd

    def _add_server(self):
        """Create the server prefix"""
        ion = self.test_info.get_defaultENV('IOF_TEST_ION')

        #Passing the list to the config file so that iof_ionss_verify
        #can access it.
        self.test_info.set_passToConfig('IOF_TEST_ION', ion)
        if ion:
            return ['-H', ion, '-N', '1']
        return ['-np', '1']

    def _add_client(self):
        """Create the client prefix"""
        cn = self.test_info.get_defaultENV('IOF_TEST_CN')
        if cn:
            return ['-H', cn, '-N', '1']
        return ['-np', '1']

    def setup_env(self):
        """setup environment variablies"""
        os.environ['CRT_PHY_ADDR_STR'] = \
            self.test_info.get_defaultENV('CRT_PHY_ADDR_STR', "ofi+sockets")
        os.environ['OFI_INTERFACE'] = \
            self.test_info.get_defaultENV('OFI_INTERFACE', "eth0")
        os.environ['D_LOG_MASK'] = \
            self.test_info.get_defaultENV('D_LOG_MASK', "INFO")

    def launch_process(self):
        """Launch the CNSS and IONSS processes"""
        self.logger.info("Testnss: Launch the CNSS and IONSS processes")
        envs = ['CRT_PHY_ADDR_STR', 'OFI_INTERFACE', 'D_LOG_MASK']
        self.setup_env()
        self.proc = None
        self.create_cnss_dir()
        self.manage_ionss_dir()
        cmd = self._add_prefix_logdir()
        test_path = self.test_info.get_defaultENV('IOF_TEST_BIN')

        logfileout = os.path.join(self.dir_path, "iofRunner.out")
        logfileerr = os.path.join(self.dir_path, "iofRunner.err")
        cmd.extend(['-x', 'CNSS_PREFIX'])
        for e in envs:
            cmd.extend(['-x', e])
        cmd.extend(self._add_client())
        cmd.append(os.path.join(test_path, 'cnss'))
        cmd.append(':')
        cmd.extend(['-x', 'ION_TEMPDIR'])
        for e in envs:
            cmd.extend(['-x', e])
        cmd.extend(self._add_server())
        cmd.append(os.path.join(test_path, 'ionss'))
        cmd.append('--config={}' \
                   .format(self.test_info.get_passToConfig("ION_CONFIG")))
        cmdstr = ' '.join(cmd)
        self.logger.info("Testionss: %s", cmdstr)
        with open(logfileout, mode='w') as outfile, \
            open(logfileerr, mode='w') as errfile:
            outfile.write("{!s}\n  Command: {!s} \n{!s}\n".format(
                ("=" * 40), cmdstr, ("=" * 40)))
            outfile.flush()
            self.proc = subprocess.Popen(cmd,
                                         stdin=subprocess.DEVNULL,
                                         stdout=outfile,
                                         stderr=errfile)
        return not self.proc

    def stop_process(self):
        """Remove temporary dirs and stale mountpoints.
        Wait for processes to terminate and terminate them after
        the wait period."""
        self.logger.info("Testionss: - stopping processes :%s", self.proc.pid)
        #self.remove_cnss_ionss_dir()
        self.stop_cnss_ionss_processes("ionss", "IOF_TEST_ION")
        self.proc.poll()
        procrtn = self.proc.returncode
        if procrtn is None:
            procrtn = -1
            try:
                self.proc.terminate()
                self.proc.wait(2)
                procrtn = self.proc.returncode
            except ProcessLookupError:
                pass
            except Exception:
                self.logger.error("Killing processes: %s", self.proc.pid)
                self.proc.kill()

        self.remove_cnss_ionss_dir()
        os.environ.pop("CNSS_PREFIX")
        os.environ.pop("ION_TEMPDIR")
        os.environ.pop("CRT_PHY_ADDR_STR")
        os.environ.pop("OFI_INTERFACE")
        os.environ.pop("D_LOG_MASK")
        self.logger.info("Testionss: - return code: %s\n", procrtn)
        return procrtn

    def stop_cnss_ionss_processes(self, proc_name, node_type):
        """Kill the remote CNSS process and IONSS processes in case the
        test's cleanup failed to kill them. The node_control executes the
        command on the remote nodes specified by the node_type."""
        testmsg = "check for any %s processes" % proc_name
        cmdstr = "pgrep -la %s" % proc_name
        procrtn = self.node_control.execute_remote_cmd(cmdstr, self.dir_path,
                                                       node_type, testmsg)
        if not procrtn:
            self.logger.error(
                "TestIOF: the %s process was still running. \
                 Error code: %d ", proc_name, procrtn)
            testmsg = "terminate any %s processes" % proc_name
            cmdstr = "pkill %s" % proc_name
            self.node_control.execute_remote_cmd(cmdstr, self.dir_path,
                                                 node_type, testmsg)

    def remove_cnss_ionss_dir(self):
        """ Call fusermount to unmount the stale mounts.
        Remove the temporary files created during setup."""
        procrtn = None
        cnss_dir = self.test_info.get_passToConfig('CNSS_PREFIX')
        ionss_dir = self.test_info.get_passToConfig('ION_TEMPDIR')
        testmsg = "Unmount CNSS dirs"
        self.use_fs_list.append('.ctrl')
        for mount in self.use_fs_list:
            cnss_mp = os.path.join(cnss_dir, mount)
            cmdstr = "fusermount -u %s" % cnss_mp
            try:
                procrtn = self.node_control.execute_remote_cmd(cmdstr,
                                                               self.dir_path,
                                                               'IOF_TEST_CN',
                                                               testmsg)
            except FileNotFoundError:
                pass
            testmsg = "Remove CNSS sub dirs"
            cmdstr = "rmdir %s" % cnss_mp
            procrtn = self.node_control.execute_remote_cmd(cmdstr,
                                                           self.dir_path,
                                                           'IOF_TEST_CN',
                                                           testmsg)
        testmsg = "Remove IONSS dirs"
        cmdstr = "rm -rf " + ionss_dir
        procrtn = self.node_control.execute_remote_cmd(cmdstr, self.dir_path,
                                                       'IOF_TEST_ION', testmsg)
        if procrtn:
            self.logger.error(
                "TestIOF: Failed to remove IONSS dirs. Error code: %d", procrtn)
        testmsg = "Remove CNSS dirs"
        cmdstr = "rm -rf " + cnss_dir
        procrtn = self.node_control.execute_remote_cmd(cmdstr, self.dir_path,
                                                       'IOF_TEST_CN', testmsg)
        if procrtn:
            self.logger.error(
                "TestIOF: Failed to remove CNSS dirs. Error code: %d", procrtn)

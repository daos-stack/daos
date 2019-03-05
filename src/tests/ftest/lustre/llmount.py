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
import traceback
import sys
import json
import uuid
import subprocess
import socket
import shlex

from avocado       import Test
from avocado       import main
from avocado.utils import process

sys.path.append('./util')
sys.path.append('../util')
sys.path.append('../../../utils/py')
sys.path.append('./../../utils/py')

class AusterTest():
    name = ""
    only = None
    excepts = None
    default = None

class LustreSetup(Test):

    # used to check passwordless ssh to lustre clients
    # return 0 on success
    def sshToClients(self):
        if subprocess.call(shlex.split(self.cmdClients("uptime"))) is not 0:
            self.fail("Can't SSH to clients")
        return 0

    # prepare cmd to be passed in pdsh (lustre clients)
    def cmdClients(self, cmd):
        return self.pdsh + " " + self.clients + " '" + cmd + "'"

    # return 0 if mgs started
    # then assume lustre servers are up
    def mgsStarted(self):
        launch_cmd = "mount | grep mgs"

        rc = subprocess.call(shlex.split(self.pdsh + " " + self.mgs + " '" +
                                         (launch_cmd) + "'"))
        return rc

    # return 0 if lustre client is mounted
    def isMounted(self):
        launch_cmd = "mount | grep -i lustre | grep -i \"" + self.mountpoint + " type\""

        rc = subprocess.call(shlex.split(self.cmdClients(launch_cmd)))
        return rc

    # return 0 if mountpoint found in fstab
    def isInFstab(self):
        launch_cmd = "cat /etc/fstab"

        rc = subprocess.call(shlex.split(self.cmdClients(launch_cmd + "| grep -i " + self.mountpoint)))
        return rc

    # mount lustre on clients
    def mountLustre(self):
        mgsnid = socket.gethostbyname(self.mgs) + "@" + self.lnettype
        launch_cmd = "mount -t lustre " + mgsnid + ":/" + self.lfsname + " " + self.mountpoint

        print(launch_cmd)
        rc = subprocess.call(shlex.split(self.cmdClients(launch_cmd)))
        return rc


    """
    Check lustre status (already mounted, not mounted) and take actions
    accordingly.
    Prepare lustre sanity variables (ONLY, EXCEPT)

    """
    def setUp(self):

        # test directory where tests will run (clean in teardown)
        self.dirtest = None
        #used to resume lustre clients state in teardown
        self.lustreInitStatus = False;
        # used to resume lustre servers state in teardown
        self.lustreServerStatus = False

        self.clients = self.params.get("clients")
        self.ltestpath = self.params.get("ltestpath")
        self.llmountcfg = self.params.get("llmountcfg")
        # init of params required for llmount config file
        self.mountpoint = self.params.get("mountpoint")
        self.mgs = self.params.get("mgs")
        self.lnettype = self.params.get("nettype",'/run/createtests/filesystem/')
        self.lfsname = self.params.get("fsname",'/run/createtests/filesystem/')
        self.lfstype = self.params.get("fstype",'/run/createtests/filesystem/')
        self.lmdssize = self.params.get("mdssize",'/run/createtests/filesystem/')
        self.lmdscount = self.params.get("mdscount",'/run/createtests/filesystem/')
        self.losscount = self.params.get("osscount",'/run/createtests/filesystem/')
        self.lostcount = self.params.get("ostcount",'/run/createtests/filesystem/')
        self.lostsize = self.params.get("ostsize",'/run/createtests/filesystem/')

        self.slow = self.params.get("slow")
        self.writeconfig = self.params.get("writeconfig", "/run/createtests/")

        self.testsuites = self.params.get("test-suites")

        self.server_group = self.params.get("server_group",'/server/',
                                           'lustre_server')

        # test access to clients
        self.pdsh = self.params.get("pdsh")
        print("Checking SSH to lustre clients")
        self.sshToClients()

        self.auster = self.ltestpath + "auster"
        if not os.path.isfile(self.auster):
            self.fail("Cannot run tests without auster")

        if self.llmountcfg == None:
            self.fail("Cannot run tests without llmount config file")

        # check if llmount config file exists
        # create a config if not found
        # can be forced with writeconfig in yaml file
        if os.path.isfile(self.llmountcfg) and not self.writeconfig:
            print("llmount config file found\n")
        else:
            print("Writing new llmount config file in " + self.llmountcfg)
            self.config_write()
            self.config_deploy()

        # llmount config file found or created
        # start the servers if required
        if self.mgsStarted() != 0:
            if self.llmount() != 0:
                self.fail("lustre servers could not be started")
        else:
            self.lustreServerStatus = True

        # Lustre clients
        if self.isMounted() == 0:
            print("Lustre already mounted")
            self.lustreInitStatus = True;
        else:
            if self.isInFstab() == 0:
                print("Lustre not mounted but mountpoint found in fstab, mounting")
                subprocess.call(self.cmdClients("mount " + self.mountpoint))
            else:
                print("Lustre not started, not in fstab, trying with specified mgs, fsname and mountpoint")
                self.mountLustre()
        # lustre should be now mounted
        if self.isMounted() == 0:
            print("Lustre mounted")
        else:
            self.fail("Could not mount lustre")

        # tests suite to be run by auster
        self.test_suites = []
        for suite in self.testsuites.split():
            test = AusterTest()
            test.name = suite
            test.only = self.params.get("only", "/run/createtests/" + suite + "/")
            test.excepts = self.params.get("except", "/run/createtests/" + suite + "/")
            test.default = self.params.get("default", "/run/createtests/" + suite + "/")
            if test.only == "None":
                test.only = test.default
            self.test_suites.append(test)

        # directory to store lustre tests logs
        self.lustretestslogdir = "/tmp/"

    def tearDown(self):
        if self.dirtest is not None and os.path.exists(self.dirtest):
            clientsArr = self.clients.split(",")
            launch_cmd = "ssh " + clientsArr[0] + " rm -rf " + self.dirtest
            subprocess.call(shlex.split(launch_cmd))
        # lustre was not mounted when tests began
        if self.lustreInitStatus == False:
            subprocess.call(shlex.split(self.cmdClients("umount " + self.mountpoint)))
        # lustre servers were not started
        if self.lustreServerStatus == False:
            subprocess.call(shlex.split(self.ltestpath + "llmountcleanup.sh -f " + self.llmountcfg))

    # Used to copy llmount config file on required lustre servers
    def config_deploy(self):
        for i in self.mdsHosts:
            subprocess.call(shlex.split("scp " + self.llmountcfg + " root@" + i + ":" + self.llmountcfg))
        for i in self.ossHosts:
            subprocess.call(shlex.split("scp " + self.llmountcfg + " root@" + i + ":" + self.llmountcfg))

    def llmount(self):
        return subprocess.call(shlex.split(self.ltestpath + "llmount.sh -f " + self.llmountcfg))

    # Used to create a new llmount configuration file
    def config_write(self):

        cfg = open(self.llmountcfg, "w")
        cfg.write("PDSH=\"" + self.pdsh + "\"\n")
        cfg.write("NAME=" + self.lfsname + "\n")
        cfg.write("NETTYPE=" + self.lnettype + "\n")
        cfg.write("FSNAME=" + self.lfsname + "\n")
        cfg.write("FSTYPE=" + self.lfstype + "\n")

        cfg.write("OSTCOUNT=" + str(self.losscount) + "\n")
        cfg.write("MDSCOUNT=" + str(self.lmdscount) + "\n")
        cfg.write("MDSSIZE=" + str(self.lmdssize) + "\n")
        cfg.write("OSTSIZE=" + str(self.lostsize) + "\n")

        # we assume mgs host is always first MDS
        mds = self.params.get("host", "/run/mds/mds1/")
        cfg.write("mgs_HOST=" + mds[0] + "\n\n")
        cfg.write("MGSDEV=/root/mgs\n")
        cfg.write("MGSNID=" + self.mgs + "@" + self.lnettype + "\n")
        self.mdsHosts = []
        self.ossHosts = []
        for i in range(1, self.lmdscount+1):
            mds = self.params.get("host", "/run/mds/mds"+str(i)+"/")
            dev = self.params.get("dev", "/run/mds/mds"+str(i)+"/")
            cfg.write("mds" + str(i) + "_HOST=" +  mds[0] + "\n")
            cfg.write("MDSDEV" + str(i) + "=" + dev[0] + "\n");
            self.mdsHosts.append(mds[0])
        cfg.write("\n")
        for i in range(1, self.losscount+1):
            oss = self.params.get("host", "/run/oss/oss"+str(i)+"/")
            dev = self.params.get("dev", "/run/oss/oss"+str(i)+"/")
            cfg.write("ost" + str(i) + "_HOST=" +  oss[0] + "\n")
            cfg.write("OSTDEV" + str(i) + "=" + dev[0] + "\n");
            self.ossHosts.append(oss[0])

        cfg.write("MOUNT=" + self.mountpoint + "\n")
        cfg.write(". $LUSTRE/tests/cfg/ncli.sh\n")

        cfg.close()

    def test_config_write(self):
        """
        write lustre llmount config file
        not really a test, used for debug purposes.

        :avocado: tags=lustre,cfgwrite
        """

        self.config_write()

    # return 0 if directory successfully created
    def create_unique_directory(self, directory):
        self.dirtest = self.mountpoint + "/" + str(directory)
        launch_cmd = " mkdir " + self.dirtest
        clientsArr = self.clients.split(",")

        rc = subprocess.call(shlex.split("ssh " + clientsArr[0] + launch_cmd))
        return rc

    def results(self,ldir):
        encoding = "UTF-8"
        skipped = passed = fails = 0

        cmd = "grep -c 'name: test' " + self.lustretestslogdir + ldir + "/results.yml"
        nbtest = subprocess.check_output(shlex.split(cmd))
        nbtests = int(nbtest.rstrip().decode(encoding))

        cmd = "grep -c SKIP " + self.lustretestslogdir + ldir + "/results.yml"
        try:
            skipp = subprocess.check_output(shlex.split(cmd))
        except Exception,e:
            skipp = str(e.output)
        skipped = int(skipp.rstrip().decode(encoding))

        cmd = "grep -c PASS " + self.lustretestslogdir + ldir + "/results.yml"
        try:
            passp = subprocess.check_output(shlex.split(cmd))
        except Exception,e:
            passp = str(e.output)
        passed = int(passp.rstrip().decode(encoding))

        cmd = "grep -c FAIL " + self.lustretestslogdir + ldir + "/results.yml"
        try:
            failedp = subprocess.check_output(shlex.split(cmd))
        except Exception,e:
            failedp = str(e.output)
        fails = int(failedp.rstrip().decode(encoding))

        print("SKIP: " + str(skipped) + "\tPASS: " + str(passed) + "\tFAILED: " + str(fails) +
              "\tTotal Tests: " + str(nbtests))
        if fails is not 0:
            self.fail("Some sanity tests failed: " + fails + " / " + nbtests)

    # run test suites with auster
    # environment variables must be saved then modified with os.environ.copy
    # passing env = {} in subprocess.call/popen will break auster PATH for some reason
    def test_lustre_auster(self):
        """
        run lustre specified tests suites

        :avocado: tags=lustre,auster
        """
        DIR_UUID = uuid.uuid4()
        if self.create_unique_directory(DIR_UUID) is not 0:
            self.fail("Failed to create test directory " + str(DIR_UUID) + " in " + self.mountpoint)

        austerparams = " -f " + self.lfsname + " -v "
        if self.slow:
            austerparams += " -s "
        austerparams += " -D " + self.lustretestslogdir + str(DIR_UUID)
        for suite in self.test_suites:
            austerparams += " " + str(suite.name) + " --only \"" + str(suite.only) + "\""
            austerparams += " --except \"" + str(suite.excepts) + "\""

        print self.ltestpath + "auster " + austerparams
        envcpy = os.environ.copy()
        envcpy["DIR"] = self.dirtest
        subprocess.call([self.ltestpath + "auster"] + shlex.split(austerparams), env=envcpy)
        self.results(str(DIR_UUID))

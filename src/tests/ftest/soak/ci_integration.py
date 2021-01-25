#!/usr/bin/python
"""
  (C) Copyright 2020 Intel Corporation.

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
"""
import os
import subprocess
import re
from apricot import TestWithServers
from general_utils import pcmd
from command_utils import CommandFailure
from daos_utils import DaosCommand
from ClusterShell.NodeSet import NodeSet
from test_utils_pool import TestPool
from test_utils_container import TestContainer


class JavaCIIntegration(TestWithServers):
    """Test class Description:

       Create a DAOS pool and container and use their
       uuid's to run the java-hadoop integration tests
       using maven.

    :avocado: recursive
    """
    def __init__(self, *args, **kwargs):
        """Initialize a JavaCIIntegration object."""
        super(JavaCIIntegration, self).__init__(*args, **kwargs)
        self.pool = None
        self.container = None

    def _create_pool(self):
        """Create a TestPool object to use with ior.
        """
        # Get the pool params
        pool = TestPool(
            self.context, dmg_command=self.get_dmg_command())
        pool.get_params(self)
        # Create a pool
        pool.create()
        return pool

    def _create_cont(self, pool, path=None):
        """Create a TestContainer object to be used to create container.
           Args:
               pool (TestPool): pool object
               path (str): Unified namespace path for container
        """
        # Get container params
        container = TestContainer(pool, daos_command=DaosCommand(self.bin))
        container.get_params(self)
        if path is not None:
            container.path.update(path)
        # create container
        container.create()
        return container

    def java_version(self):
        """Check if java is installed.

        Returns:
            bool: whether java is installed or not.

        """

        # checking java install
        try:
            result = subprocess.check_output(['java', '-version'],
                                             stderr=subprocess.STDOUT)
            self.log.info(result)
        except subprocess.CalledProcessError as err:
            self.fail("Java not installed \n {}".format(err))
        # looking for a string something like this 1.8.0_262-b10
        pattern = r"(\d+\.\d+\.\d+\_\d+\-[a-b]\d+)"

        # replacing '-' and '-' with '.' and returning the result
        return re.search(pattern,
                         result).groups()[0].replace("_", ".").replace("-", ".")

    def test_java_hadoop_it(self):
        """Jira ID: DAOS-4093

        Test Description:
            Create a DAOS pool and container and use their
            uuid's to run the java-hadoop integration tests
            using maven.

        :avocado: tags=all,pr,hw,small,javaciintegration
        """
        # create pool and container
        self.pool = self._create_pool()
        self.container = self._create_cont(self.pool)

        pool_uuid = self.pool.uuid
        cont_uuid = self.container.uuid

        # obtain java version
        version = self.java_version()

        # generate jar files
        jdir = "{}/lib64/java".format(self.prefix)
#        cmd = "cd {};".format(jdir)
#        cmd += "mvn -X clean install -DskipITs -Ddaos.install.path={}".\
#                format(self.prefix)
#        cmd += " >> {}/maven_jar_generation_output.log".format(self.log_dir)
#        self.execute_cmd(cmd, 180)


        # run intergration-test
#        jdir = "{}/java".format(os.getcwd())
        cmd = "cd {};".format(jdir)
        cmd += " export LD_PRELOAD=/usr/lib/jvm/"
        cmd += "java-{}-openjdk-{}-0.el7_8.x86_64/".format(version[:-8],
                                                           version)
        cmd += "jre/lib/amd64/libjsig.so;"
        cmd += " mvn -X clean integration-test -Ddaos.install.path={}".\
                format(self.prefix)
        cmd += " -Dpool_id={}  -Dcont_id={}".format(pool_uuid, cont_uuid)
        cmd += " >> {}/maven_integration_test_output.log".format(self.log_dir)
        self.execute_cmd(cmd, 300)

    def execute_cmd(self, cmd, timeout):
        """Execute command on the host clients
           Args:
               cmd (str): Command to run
        """

        try:
            # execute bash cmds
            ret = pcmd(
                self.hostlist_servers, cmd, verbose=True, timeout=timeout)
            if 0 not in ret:
                error_hosts = NodeSet(
                    ",".join(
                        [str(node_set) for code, node_set in
                         ret.items() if code != 0]))
                raise CommandFailure(
                    "Error running '{}' on the following "
                    "hosts: {}".format(cmd, error_hosts))

         # report error if any command fails
        except CommandFailure as error:
            self.log.error("JavaCIIntegration Test Failed: %s",
                           str(error))
            self.fail("Test was expected to pass but "
                      "it failed.\n")
        return ret

#!/usr/bin/python3
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
from general_utils import error_count
from java_utils import JavaUtils

class JavaCIIntegration(JavaUtils):
    """Test class Description:

       Create a DAOS pool and container and use their
       uuid's to run the java-hadoop integration tests
       using maven.

    :avocado: recursive
    """

    def test_java_hadoop_it(self):
        """Jira ID: DAOS-4093

        Test Description:
            Create a DAOS pool and container and use their
            uuid's to run the java-hadoop integration tests
            using maven.

        :avocado: tags=all,pr,hw,small,javaciintegration
        """
        # Copy built java folder to /var/tmp.
        # Need to be copied to perform maven operations.
        test_jdir = str("{}/../java".format(os.getcwd()))
        cp_cmd = "cp -r {} {}/..".format(test_jdir, self.log_dir)
        self.run_command(self.hostlist_clients, [cp_cmd])

        # get java dir
        self.jdir = str("{}/../java".format(self.log_dir))

        # create pool and container
        pool = self._create_pool()
        container = self._create_cont(pool)

        # obtain java version
        version = self.java_version()

        # output file
        output_file = "{}/maven_integration_test_output.log".\
                       format(self.log_dir)

        # run intergration-test
        cmd = "cd {};".format(self.jdir)
        cmd += " export LD_PRELOAD={}/jre/lib/amd64/libjsig.so;".format(version)
        cmd += " ls -l daos-java/target/antrun/build-compile-proto.xml;"
        cmd += " mvn -X integration-test -Ddaos.install.path={}".\
                format(self.prefix)
        cmd += " -Dpool_id={}  -Dcont_id={} ".format(pool.uuid, container.uuid)
        cmd += " >> {}".format(output_file)
        print("***{}".format(cmd))

        self.run_command(self.hostlist_clients, [cmd], True, 120)
        # look for errors
        found_errors = error_count("Errors: [1-9]+", self.hostlist_clients,
                                   output_file, 'Errors: [0-9]')
        self.log.info("Matched error lines: %s", found_errors[0])

        if found_errors[0] > 0:
            self.fail("Java Integration test failed. Check"
                      " maven_integration_test_output.log for more details.")

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
from avocado.utils import genio
import aexpect
from apricot import Test

def print_helper(thestring):
    """
    Print helper function.
    """
    print(thestring)

CPU_COUNT = 0

class ServerLaunch(Test):
    """
    Tests launching a DAOS server.

    :avocado: recursive
    """

    def test_launch(self):
        """
        Test launching a DAOS server.
        :avocado: tags=server
        """
        hostfile = self.params.get("hostfile1", '/files/', "/tmp/hostfile1")
        urifile = self.params.get("urifile", '/files/', "/tmp/urifile")

        server_count = len(genio.read_all_lines(hostfile))

        get_prompt = "/bin/bash"
        launch_cmd = "../../install/bin/orterun --np {0} ".format(server_count)
        launch_cmd += "--hostfile {0} --enable-recovery ".format(hostfile)
        launch_cmd += (
            "--report-uri {0} "
            "-x D_LOG_FILE=/mnt/shared/test/tmp/daos.log "
            "-x LD_LIBRARY_PATH=/home/skirvan/daos_m10/install/lib"
            ":/home/skirvan/daos_m10/install/lib/daos_srv "
            "../../install/bin/daos_server -d /tmp/.daos "
            "-g daos_server".format(urifile)
            )

        try:
            session = aexpect.ShellSession(get_prompt)
            if session.is_responsive():
                session.sendline(launch_cmd)
                session.read_until_any_line_matches(
                    "XDAOS server (v0.0.2) started on rank *",
                    timeout=5.0,
                    print_func=print_helper)
            else:
                self.fail("Server did not start.\n")
        except (aexpect.ExpectError, aexpect.ExpectProcessTerminatedError,
                aexpect.ExpectTimeoutError, aexpect.ShellCmdError,
                aexpect.ShellError, aexpect.ShellProcessTerminatedError,
                aexpect.ShellStatusError, aexpect.ShellTimeoutError) as dummy_e:
            self.fail("Server did not start.\n")

        session.sendcontrol("c")

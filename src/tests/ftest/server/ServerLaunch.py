#!/usr/bin/python

import os
import time

from apricot import Test
from avocado import main
from avocado.utils import process
from avocado.utils import git
from avocado.utils import cpu
from avocado.utils import genio
import aexpect
from aexpect.client import run_bg

def printFunc(thestring):
        print(thestring)

cpu_count = 0

class ServerLaunch(Test):
    """
    Tests launching a DAOS server.

    :avocado: recursive
    """

    def test_launch(self):
        """
        :avocado: tags=server
        """
        host = self.params.get("hostname",'/tests/', "localhost")
        hostfile = self.params.get("hostfile1",'/files/',"/tmp/hostfile1")
        urifile = self.params.get("urifile",'/files/',"/tmp/urifile")

        server_count = len(genio.read_all_lines(hostfile))

        get_prompt = "/bin/bash"
        launch_cmd = "../../install/bin/orterun --np {0} ".format(
                server_count)
        launch_cmd += "--hostfile {0} --enable-recovery ".format(hostfile)
        launch_cmd += "--report-uri {0} -x D_LOG_FILE=/mnt/shared/test/tmp/daos.log ".format(urifile)
        launch_cmd += "-x LD_LIBRARY_PATH=/home/skirvan/daos_m10/install/lib:/home/skirvan/daos_m10/install/lib/daos_srv "
        launch_cmd += "../../install/bin/daos_server -d /tmp/.daos -g daos_server"

        try:
            session = aexpect.ShellSession(get_prompt)
            if (session.is_responsive()):
                  session.sendline(launch_cmd)
                  session.read_until_any_line_matches(
                         "XDAOS server (v0.0.2) started on rank *", timeout=5.0, print_func=printFunc)
            else:
                self.fail("Server did not start.\n")
        except Exception as e:
                self.fail("Server did not start.\n")

        session.sendcontrol("c")
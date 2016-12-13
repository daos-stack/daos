#!/usr/bin/env python3
# Copyright (C) 2016-2017 Intel Corporation
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# -*- coding: utf-8 -*-
"""
cart self test

"""

import os
import time
import commontestsuite

testsuite = "Test Group"
testprocess = "self_test"

class SelfTest(commontestsuite.CommonTestSuite):
    """ Execute self_test tests """
    pass_env = " -x CCI_CONFIG -x CRT_LOG_MASK "

    def test_self_test(self):
        """Run self test over two nodes"""

        # Ensure that DVM is running, as this test requires launching two jobs
        # under the same environment.
        if not os.getenv('TR_USE_URI', ""):
            self.skipTest('requires DVM to run.')

        testmsg = self.shortDescription()
        (cmd, prefix) = self.common_add_prefix_logdir(testprocess)
        (server, client) = self.common_add_server_client()

        # First launch a test_group instance to act as a target for the self
        # test.  This doesn't need to do anything other than be there and
        # remain running for the duration.
        server_args = "--name target --hold --is_service"
        server_cmd = "%s -n 1 %s %s %s tests/test_group %s" % \
        (cmd, server, self.pass_env, prefix, server_args)

        server_proc = self.common_launch_process(testsuite, testmsg, server_cmd)

        time.sleep(2)

        # Now define the self_test options.
        message_sizes = "1,4,16,1024"
        rpcs_in_flight = 16
        repetitions = 100

        client_args = "--group-name target --endpoint 0:0 " + \
            "--message-sizes %s --max-inflight-rpcs %d --repetitions %d" % \
            (message_sizes, rpcs_in_flight, repetitions)

        cmdstr = "%s %s-n 1 %s%s tests/self_test --group-name target %s" % \
          (cmd, client, self.pass_env, prefix, client_args)

        # Launch, and wait for the self-test itself.  This is where the actual
        # code gets run.  Launch and keep the return code, but do not check it
        # until after the target has been stopped.
        procrtn = self.common_launch_test(testsuite, testmsg, cmdstr)

        # Stop the server.  This will normally run forever because of the hold
        # option, so allow stop_process() to kill it but do not check the
        # return code.
        self.common_stop_process_now(testsuite, testmsg, server_proc)

        if procrtn:
            self.fail("Self test failed with %d" % procrtn)

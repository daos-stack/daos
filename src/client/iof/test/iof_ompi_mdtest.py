#!/usr/bin/env python3
# Copyright (C) 2017 Intel Corporation
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

"""
test runner test

"""

import os
import logging
#pylint: disable=import-error
import NodeControlRunner
#pylint: enable=import-error


class TestMdtest():
    """Simple python test"""
    def __init__(self, test_info=None, log_base_path=None):
        self.test_info = test_info
        self.info = test_info.info
        self.log_dir_base = log_base_path
        self.logger = logging.getLogger("TestRunnerLogger")

    def useLogDir(self, log_path):
        """create the log directory name"""
        self.log_dir_base = log_path

    def test_mdtest_ompi(self):
        """Simple ping"""
        proc_rtn = 1
        self.logger.info("Test mdtest")
        testname = self.test_info.get_test_info('testName')
        testlog = os.path.join(self.log_dir_base, testname)
        nodes = NodeControlRunner.NodeControlRunner(testlog, self.test_info)
        prefix = self.test_info.get_defaultENV('IOF_OMPI_BIN', "")
        cmd_list = nodes.start_cmd_list(self.log_dir_base, testname, prefix)
        cmd_list.add_nodes('IOF_TEST_CN')
        env_vars = {}
        env_vars['LD_LIBRARY_PATH'] = \
            self.test_info.get_defaultENV('LD_LIBRARY_PATH')
        cmd_list.add_env_vars(env_vars)
        parameters = "-i 3 -I 10 -d {!s}/FS_2".format(
            self.test_info.get_defaultENV('CNSS_PREFIX'))
        cmdstr = "{!s}/mdtest".format(
            self.test_info.parameters_one("{mdtest_path}"))
        cmd_list.add_cmd(cmdstr, parameters)
        cmd_list.start_process()
        if cmd_list.check_process():
            proc_rtn = cmd_list.wait_process()
        return proc_rtn

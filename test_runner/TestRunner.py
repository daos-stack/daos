#!/usr/bin/env python3
# Copyright (c) 2016 Intel Corporation
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
test runner class

"""

#pylint: disable=unused-import

import os
import sys
import subprocess
import unittest
import json
from datetime import datetime

from yaml import load, dump
try:
    from yaml import CLoader as Loader, CDumper as Dumper
except ImportError:
    from yaml import Loader, Dumper


class TestRunner():
    """Simple test runner"""
    log_dir_base = ""
    loop_name = ""
    last_testlogdir = ""
    loop_number = 0
    test_info = {}
    test_list = []
    info = None

    def __init__(self, info, test_list=None):
        self.info = info
        self.test_list = test_list
        self.log_dir_base = self.info.get_config('log_base_path')

    def set_key_from_host(self):
        """ add to default environment """
        module = self.test_info['module']
        host_list = self.info.get_config('host_list')
        hostkey_list = module.get('setKeyFromHost')
        for k in range(0, len(hostkey_list)):
            self.test_info['defaultENV'][hostkey_list[k]] = host_list[k]

    def set_key_from_info(self):
        """ add to default environment """
        module = self.test_info['module']
        key_list = module.get('setKeyFromInfo')
        for item in range(0, len(key_list)):
            (k, v, ex) = key_list[item]
            self.test_info['defaultENV'][k] = self.info.get_info(v) + ex

    def create_append_key_from_info(self, append=False):
        """ add to default environment """
        save_value = ""
        module = self.test_info['module']
        key_list = module.get('appendKeyFromInfo')
        for var in range(0, len(key_list)):
            (k, ex, vlist) = key_list[var]
            if append:
                save_value = os.getenv(k)
            new_list = []
            for var_name in vlist:
                var_value = self.info.get_info(var_name) + ex
                if var_value not in save_value:
                    new_list.append(var_value)
            items = ":"
            new_value = items.join(new_list)
            if save_value:
                self.test_info['defaultENV'][k] = \
                    new_value + ":" + save_value
            else:
                self.test_info['defaultENV'][k] = new_value

    def set_key_from_config(self):
        """ add to default environment """
        config_key_list = self.info.get_config('setKeyFromConfig')
        for (key, value) in config_key_list.items():
            self.test_info['defaultENV'][key] = value

    def add_default_env(self):
        """ add to default environment """
        module = self.test_info['module']
        if self.info.get_config('host_list') and module.get('setKeyFromHost'):
            self.set_key_from_host()
        if module.get('setKeyFromInfo'):
            self.set_key_from_info()
        if module.get('createKeyFromInfo'):
            self.create_append_key_from_info()
        if module.get('appendKeyFromInfo'):
            self.create_append_key_from_info(True)
        if self.info.get_config('setKeyFromConfig'):
            self.set_key_from_config()

    def setup_default_env(self):
        """ setup default environment """
        module_env = self.test_info['defaultENV']
        for (key, value) in module_env.items():
            os.environ[str(key)] = value

    @staticmethod
    def setenv(testcase):
        """ setup testcase environment """
        module_env = testcase['setEnvVars']
        if module_env != None:
            for (key, value) in module_env.items():
                os.environ[str(key)] = value

    def resetenv(self, testcase):
        """ reset testcase environment """
        if os.getenv('TR_USE_VALGRIND', "") == "callgrind":
            self.callgrind_annotate()
        module_env = testcase['setEnvVars']
        module_default_env = self.test_info['defaultENV']
        if module_env != None:
            for key in module_env.keys():
                value = module_default_env.get(key, "")
                os.environ[str(key)] = value

    def settestlog(self, testcase_id):
        """ setup testcase environment """
        test_module = self.test_info['module']
        value = self.log_dir_base + "/" + \
                self.loop_name + "_loop" + str(self.loop_number) + "/" + \
                test_module['name'] + "_" + str(testcase_id)
        os.environ[test_module['subLogKey']] = value
        self.last_testlogdir = value

    def dump_test_info(self):
        """ dump the test info to the output directory """
        if os.path.exists(self.log_dir_base):
            name = "%s/%s_test_info.yml" % (self.log_dir_base, self.loop_name)
            with open(name, 'w') as fd:
                dump(self.test_info, fd, Dumper=Dumper, indent=4,
                     default_flow_style=False)

    def rename_output_directory(self):
        """ rename the output directory """
        if os.path.exists(self.log_dir_base):
            newname = "%s_%s" % \
                      (self.log_dir_base, datetime.now().isoformat())
            os.rename(self.log_dir_base, newname)
            print("TestRunner: test log directory\n %s" % newname)

    def callgrind_annotate(self):
        """ If callgrind is used pull in the results """
        print("TestRunner: Callgrind annotate begin")
        module = self.test_info['module']
        srcdir = module.get('srcDir', "")
        src_rootdir = self.info.get_info('SRCDIR')
        if srcdir and src_rootdir:
            if isinstance(srcdir, str):
                srcfile = " %s/%s/*.c" % (src_rootdir, srcdir)
            else:
                srcfile = ""
                for item in srcdir:
                    srcfile += " %s/%s/*.c" % (src_rootdir, item)
            dirlist = os.listdir(self.last_testlogdir)
            for infile in dirlist:
                if os.path.isfile(infile) and infile.find(".out"):
                    outfile = infile.replace("out", "gp.out")
                    cmdarg = "callgrind_annotate " + infile + srcfile
                    print(cmdarg)
                    with open(outfile, 'w') as out:
                        subprocess.call(cmdarg, timeout=30, shell=True,
                                        stdout=out,
                                        stderr=subprocess.STDOUT)
        else:
            print("TestRunner: Callgrind no source directory")
        print("TestRunner: Callgrind annotate end")

    def post_run(self):
        """ post run processing """
        print("TestRunner: tearDown begin")
        self.rename_output_directory()
        print("TestRunner: tearDown end\n\n")

    def dump_error_messages(self, testMethodName):
        """dump the ERROR tag from stdout file"""
        dirname = testMethodName.split('_', maxsplit=2)
        newdir = os.path.join(self.last_testlogdir, dirname[2])
        if not os.path.exists(newdir):
            print("Directory not found: %s" % newdir)
            return
        dirlist = sorted(os.listdir(newdir), reverse=True)
        for psdir in dirlist:
            dname = os.path.join(newdir, psdir)
            if os.path.isfile(dname):
                continue
            rankdirlist = sorted(os.listdir(dname), reverse=True)
            for rankdir in rankdirlist:
                dumpstdout = os.path.join(newdir, psdir, rankdir, "stdout")
                dumpstderr = os.path.join(newdir, psdir, rankdir, "stderr")
                print("******************************************************")
                print("Error info from file\n %s" % dumpstdout)
                filesize = os.path.getsize(dumpstdout) > 1024
                if filesize > 1024:
                    print("File too large (%d bytes), showing errors only" % \
                          filesize)
                    with open(dumpstdout, 'r') as file:
                        for line in file:
                            if line.startswith("ERROR:"):
                                print(line)
                else:
                    with open(dumpstdout, 'r') as file:
                        print(file.read())

                print("Error info from file\n %s" % dumpstderr)
                if os.path.getsize(dumpstderr) > 0:
                    with open(dumpstderr, 'r') as file:
                        print(file.read())
                else:
                    print("--- empty ---\n")

    def execute_list(self):
        """ execute test scripts """

        rtn = 0
        test_module = self.test_info['module']
        for testrun in self.test_info['execStrategy']:
            print("************** run %s ******************************" % \
                  testrun['id'])
            if 'setEnvVars' in testrun:
                self.setenv(testrun)
            self.settestlog(testrun['id'])
            suite = \
                unittest.TestLoader().loadTestsFromName(test_module['name'])
            results = unittest.TestResult()
            suite.run(results)

            print("***************** Results *********************************")
            print("Number test run: %s" % results.testsRun)
            if results.wasSuccessful() is True:
                print("Test was successful\n")
            else:
                rtn |= 1
                print("Test failed")
                print("\nNumber test errors: %d" % len(results.errors))
                for error_item in results.errors:
                    print(error_item[0])
                    print(error_item[1])
                print("\nNumber test failures: %d" % len(results.failures))
                for results_item in results.failures:
                    print(results_item[0])
                    print(results_item[1])
                    test_object_dict = results_item[0].__dict__
                    self.dump_error_messages(
                        test_object_dict['_testMethodName'])

            print("***********************************************************")
            if 'setEnvVars' in testrun:
                self.resetenv(testrun)

        return rtn

    def execute_strategy(self):
        """ execute test strategy """

        rtn = 0
        test_directives = self.test_info.get('directives', None)
        print("***************** %s *********************************" % \
              self.test_info['module']['name'])
        loop = str(test_directives.get('loop', "no"))
        if loop.lower() == "no":
            self.loop_number = 0
            rtn = self.execute_list()
        else:
            for i in range(int(loop)):
                print("***************loop %d *************************" % i)
                self.loop_number = i
                rtn |= self.execute_list()
                toexit = test_directives.get('exitLoopOnError', "yes")
                if rtn and toexit.lower() == "yes":
                    break
        return rtn

    def run_testcases(self):
        """ execute test scripts """

        sys.path.append("scripts")
        rtn = 0
        print("\n*************************************************************")
        for test_module_name in self.test_list:
            self.loop_name = os.path.splitext(
                os.path.basename(test_module_name))[0]
            self.test_info.clear()
            with open(test_module_name, 'r') as fd:
                self.test_info = load(fd, Loader=Loader)
            self.add_default_env()
            self.setup_default_env()
            rtn |= self.execute_strategy()
            self.dump_test_info()
        self.post_run()
        return rtn

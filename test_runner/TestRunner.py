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
#pylint: disable=too-many-instance-attributes
#pylint: disable=too-many-locals
#pylint: disable=too-many-public-methods

import os
import sys
import subprocess
import shutil
import unittest
import json
import tempfile
from time import time
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
    subtest_results = []
    test_directives = {}
    info = None

    def __init__(self, info, test_list=None):
        self.info = info
        self.test_list = test_list
        self.log_dir_base = self.info.get_config('log_base_path')
        self.test_directives = self.test_info.get('directives', {})

    def set_key_from_host(self):
        """ add to default environment """
        module = self.test_info['module']
        host_list = self.info.get_config('host_list')
        hostkey_list = module.get('setKeyFromHost')
        host_config = module.get('hostConfig')
        if not host_config or host_config['type'] == 'oneToOne':
            for k in range(0, len(hostkey_list)):
                self.test_info['defaultENV'][hostkey_list[k]] = host_list[k]
        elif host_config['type'] == 'buildList':
            items = ","
            end = host_config['numServers']
            server_list = items.join(host_list[0:end])
            self.test_info['defaultENV'][hostkey_list[0]] = server_list
            start = host_config['numServers']
            end = start + host_config['numClients']
            client_list = items.join(host_list[start:end])
            self.test_info['defaultENV'][hostkey_list[1]] = client_list

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
        if append:
            key_list = module.get('appendKeyFromInfo')
        else:
            key_list = module.get('createKeyFromInfo')
        for var in range(0, len(key_list)):
            (k, ex, vlist) = key_list[var]
            if append:
                save_value = self.test_info['defaultENV'].get(k, (os.getenv(k)))
            new_list = []
            for var_name in vlist:
                var_value = self.info.get_info(var_name) + ex
                if not save_value or var_value not in save_value:
                    new_list.append(var_value)
            items = ":"
            new_value = items.join(new_list)
            if not new_value:
                self.test_info['defaultENV'][k] = save_value
            elif save_value:
                self.test_info['defaultENV'][k] = \
                    new_value + ":" + save_value
            else:
                self.test_info['defaultENV'][k] = new_value

    def set_key_from_config(self):
        """ add to default environment """
        config_key_list = self.info.get_config('setKeyFromConfig')
        for (key, value) in config_key_list.items():
            self.test_info['defaultENV'][key] = value

    def set_directive_from_config(self):
        """ add to default test directives """
        config_key_list = self.info.get_config('setDirectiveFromConfig')
        for (key, value) in config_key_list.items():
            self.test_directives[key] = value

    def create_tmp_dir(self):
        """ add to default environment """
        envName = self.test_info['module']['createTmpDir']
        tmpdir = tempfile.mkdtemp()
        self.test_info['defaultENV'][envName] = tmpdir

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
        if self.info.get_config('setDirectiveFromConfig'):
            self.set_directive_from_config()
        if module.get('createTmpDir'):
            self.create_tmp_dir()

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

    def dump_subtest_results(self):
        """ dump the test results to the log directory """
        log_dir = os.path.dirname(self.info.get_config('log_base_path'))
        if os.path.exists(log_dir):
            name = "%s/subtest_results.yml" % log_dir
            with open(name, 'w') as fd:
                dump(self.subtest_results, fd, Dumper=Dumper, indent=4,
                     default_flow_style=False)

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
            if str(self.test_directives.get('renameTestRun', "yes")).lower() \
               == "yes":
                newname = "%s_%s" % \
                          (self.log_dir_base, datetime.now().isoformat())
            else:
                newdir = str(self.test_directives.get('renameTestRun'))
                logdir = os.path.dirname(self.log_dir_base)
                newname = os.path.join(logdir, newdir)
            os.rename(self.log_dir_base, newname)
            print("TestRunner: test log directory\n %s" % newname)

    def valgrind_memcheck(self):
        """ If memcheck is used to check the results """
        print("TestRunner: valgrind memcheck begin")
        from xml.etree import ElementTree
        rtn = 0
        error_str = ""
        newdir = self.last_testlogdir
        if not os.path.exists(newdir):
            print("Directory not found: %s" % newdir)
            return
        dirlist = os.listdir(newdir)
        for psdir in dirlist:
            dname = os.path.join(newdir, psdir)
            if os.path.isfile(dname):
                continue
            testdirlist = os.listdir(dname)
            for fname in testdirlist:
                if str(fname).endswith(".xml"):
                    with open(os.path.join(dname, fname), "r") as xmlfile:
                        tree = ElementTree.parse(xmlfile)
                    error_types = {}
                    for node in tree.iter('error'):
                        kind = node.find('./kind')
                        if not kind.text in error_types:
                            error_types[kind.text] = 0
                        error_types[kind.text] += 1
                    if error_types:
                        error_str += "test dir: %s  file: %s\n" % (psdir, fname)
                        for err in error_types:
                            error_str += "%-3d %s errors\n"%(error_types[err],
                                                             err)
        if error_str:
            print("""
#########################################################
    memcheck TESTS failed.
%s
#########################################################
""" % error_str)
            rtn = 1
        print("TestRunner: valgrind memcheck end")
        return rtn

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
        self.dump_subtest_results()
        if self.test_info['module'].get('createTmpDir'):
            envName = self.test_info['module']['createTmpDir']
            shutil.rmtree(self.test_info['defaultENV'][envName])
        if str(self.test_directives.get('renameTestRun', "yes")).lower() \
           != "no":
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

            use_valgrind = os.getenv('TR_USE_VALGRIND', "")
            if use_valgrind == "memcheck" and \
               str(self.test_directives.get('checkXml', "no")).lower() == "yes":
                self.valgrind_memcheck()
            elif use_valgrind == "callgrind":
                self.callgrind_annotate()

            print("***********************************************************")
            if 'setEnvVars' in testrun:
                self.resetenv(testrun)

        return rtn

    def execute_strategy(self):
        """ execute test strategy """

        info = {}
        rtn = 0
        info['name'] = self.test_info['module']['name']
        print("***************** %s *********************************" % \
              info['name'])
        loop = str(self.test_directives.get('loop', "no"))
        start_time = time()
        if loop.lower() == "no":
            self.loop_number = 0
            rtn = self.execute_list()
        else:
            for i in range(int(loop)):
                print("***************loop %d *************************" % i)
                self.loop_number = i
                rtn |= self.execute_list()
                toexit = self.test_directives.get('exitLoopOnError', "yes")
                if rtn and toexit.lower() == "yes":
                    break
        info['duration'] = time() - start_time
        info['return_code'] = rtn
        if rtn == 0:
            info['status'] = "PASS"
        else:
            info['status'] = "FAIL"
        info['error'] = ""
        return info

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
            rtn_info = self.execute_strategy()
            rtn |= rtn_info['return_code']
            self.subtest_results.append(rtn_info)
            self.dump_test_info()
        self.post_run()
        return rtn

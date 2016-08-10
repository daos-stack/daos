#!/usr/bin/env python3
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
    log_dir_base = "/tmp/testLogs/testRun"
    loop_name = ""
    last_testlogdir = ""
    loop_number = 0
    config = {}
    info = {}
    test_info = {}
    test_list = []

    def __init__(self, config=None, test_list=None):
        self.config = config
        self.test_list = test_list

    def env_setup(self):
        """ setup the environment """
        print("TestRunner: setUp env begin")

        rootpath = os.getcwd()
        print("path: %s" % rootpath)
        platform = os.uname()[0]
        opts_file = rootpath + "/.build_vars.json"
        print("use file: %s" % opts_file)
        with open(opts_file, "r") as info_file:
            self.info = json.load(info_file)

        ompi_path = self.info['OMPI_PREFIX'] + "/bin"
        path = os.getenv("PATH")
        if path.find(ompi_path) < 0:
            path = ompi_path + ":" + path
        if 'MCL_PREFIX' in self.info:
            mcl_path = self.info['MCL_PREFIX'] + "/bin"
            if path.find(mcl_path) < 0:
                path = mcl_path + ":" + path
        installed_path = self.info['PREFIX']
        test_path = installed_path + "/TESTING/tests"
        if path.find(test_path) < 0:
            path = test_path + ":" + path
        bin_path = installed_path + "bin"
        if path.find(bin_path) < 0:
            path = bin_path + ":" + path
        os.environ['PATH'] = path
        if platform == "Darwin":
            self.setup_Darwin()
        self.rename_output_directory()
        os.makedirs(self.log_dir_base)
        print("TestRunner: setUp end\n")

    def setup_Darwin(self):
        """ setup mac OS environment """
        os.environ['OMPI_MCA_orte_tmpdir_base'] = "/tmp"
        dyld = os.getenv("DYLD_LIBRARY_PATH", default="")
        lib_paths = []
        for key in sorted(self.info.keys()):
            if not isinstance(self.info[key], str):
                continue
            if not "PREFIX" in key:
                continue
            if self.info[key] == "/usr":
                continue
            lib = os.path.join(self.info[key], "lib")
            lib64 = os.path.join(self.info[key], "lib64")
            if os.path.exists(lib) and lib not in lib_paths:
                lib_paths.insert(0, lib)
            if os.path.exists(lib64) and lib64 not in lib_paths:
                lib_paths.insert(0, lib64)
        new_lib_path = os.pathsep.join(lib_paths) + dyld
        os.environ['DYLD_LIBRARY_PATH'] = new_lib_path
        print("DYLD_LIBRARY_PATH = %s" % new_lib_path)

    def add_default_env(self):
        """ add to default environment """
        module = self.test_info['module']
        if self.config:
            try:
                host_list = self.config['host_list']
                key_list = module['setKeyFromHost']
                for k in range(0, len(key_list)):
                    self.test_info['defaultENV'][key_list[k]] = host_list[k]
            except KeyError:
                pass
        key_list = module.get('setKeyFromInfo')
        if key_list:
            for item in range(0, len(key_list)):
                (k, v, ex) = key_list[item]
                self.test_info['defaultENV'][k] = self.info[v] + ex

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
        src_rootdir = self.info.get('SRCDIR', "")
        if srcdir and src_rootdir:
            if isinstance(srcdir, str):
                srcfile = " %s/%s/*.c" % (src_rootdir, srcdir)
            else:
                srcfile = ""
                for item in srcdir:
                    srcfile += " %s/%s/*.c" % (src_rootdir, item)
            newdir = os.path.join(self.log_dir_base, self.last_testlogdir)
            dirlist = os.listdir(newdir)
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
        newdir = os.path.join(self.log_dir_base, self.last_testlogdir,
                              dirname[2])
        if not os.path.exists(newdir):
            print("Directory not found: %s" % newdir)
            return
        dirlist = sorted(os.listdir(newdir), reverse=True)
        for psdir in dirlist:
            rankdirlist = sorted(os.listdir(os.path.join(newdir, psdir)),
                                 reverse=True)
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
        self.env_setup()
        for test_module_name in self.test_list:
            self.loop_name = os.path.splitext(
                os.path.basename(test_module_name))[0]
            self.test_info.clear()
            with open(test_module_name, 'r') as fd:
                self.test_info = load(fd, Loader=Loader)
            if 'setKeyFromHost' in self.test_info['module']:
                self.add_default_env()
            self.setup_default_env()
            rtn |= self.execute_strategy()
            self.dump_test_info()
        self.post_run()
        return rtn

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
test runner test info class

"""

import os
import logging
#pylint: disable=import-error
import PreRunner
#pylint: enable=import-error
from yaml import load, dump
try:
    from yaml import CLoader as Loader, CDumper as Dumper
except ImportError:
    from yaml import Loader, Dumper


class TestInfoRunner(PreRunner.PreRunner):
    """Simple test runner"""
    info = None
    test_info = {}

    def __init__(self, info=None):
        self.info = info
        self.log_dir_base = self.info.get_config('log_base_path')
        self.logger = logging.getLogger("TestRunnerLogger")

    def load_testcases(self, test_module_name):
        """ load and check test description file """

        rtn = 0
        with open(test_module_name, 'r') as fd:
            self.test_info = load(fd, Loader=Loader)

        if 'description' not in self.test_info:
            self.logger.error(" No description defined in file: %s", \
                             test_module_name)
            rtn = 1
        if 'module' not in self.test_info:
            self.logger.error(" No module section defined in file: %s", \
                             test_module_name)
            rtn = 1
        if 'execStrategy' not in self.test_info:
            self.logger.error(" No execStrategy section defined in file: %s",
                              test_module_name)
            rtn = 1
        if 'defaultENV' not in self.test_info or \
           self.test_info['defaultENV'] is None:
            self.test_info['defaultENV'] = {}
        if 'directives' not in self.test_info or \
           self.test_info['directives'] is None:
            self.test_info['directives'] = {}
        return rtn

    def post_run(self):
        """ post testcase run processing """
        self.remove_tmp_dir()
        self.dump_test_info()

    def dump_test_info(self):
        """ dump the test info to the output directory """
        if os.path.exists(self.log_dir_base):
            name = "%s/%s/%s_test_info.yml" % (self.log_dir_base,
                                               self.test_info['module']['name'],
                                               self.test_info['module']['name'])
            with open(name, 'w') as fd:
                dump(self.test_info, fd, Dumper=Dumper, indent=4,
                     default_flow_style=False)

    def get_test_info(self, keyname=None, subkey=None, default=""):
        """ setup the environment """
        if subkey:
            try:
                value = self.test_info[keyname][subkey]
            except KeyError:
                value = default
        else:
            value = self.test_info.get(keyname, default)
        return value

    def set_test_info(self, keyname=None, subkey=None, keyvalue=None):
        """ setup the environment """
        if subkey:
            if keyname not in self.test_info:
                self.test_info[keyname] = {}
            self.test_info[keyname][subkey] = keyvalue
        else:
            self.test_info[keyname] = keyvalue

    def get_defaultENV(self, keyname=None, default=""):
        """ setup the environment """
        value = self.test_info['defaultENV'].get(keyname, default)
        return value

    def set_defaultENV(self, keyname=None, keyvalue=None):
        """ setup the environment """
        self.test_info['defaultENV'][keyname] = keyvalue

    def get_module(self, keyname=None, default=""):
        """ return information from the module section """
        if keyname:
            value = self.test_info['module'].get(keyname, default)
        else:
            value = self.test_info['module']
        return value

    def get_directives(self):
        """ return the test directives """
        return self.test_info['directives']

    def get_execStrategy(self):
        """ return the execStrategy list """
        return self.test_info['execStrategy']

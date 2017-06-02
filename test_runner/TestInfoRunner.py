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
import re
import logging
#pylint: disable=import-error
import PreRunner
#pylint: enable=import-error
from socket import gethostname
from yaml import load, dump
try:
    from yaml import CLoader as Loader, CDumper as Dumper
except ImportError:
    from yaml import Loader, Dumper

#pylint: disable=consider-using-enumerate
#pylint: disable=anomalous-backslash-in-string


class TestInfoRunner(PreRunner.PreRunner):
    """Simple test runner"""
    info = None
    test_info = {}

    def __init__(self, info=None):
        self.info = info
        self.log_dir_base = self.info.get_config('log_base_path')
        self.logger = logging.getLogger("TestRunnerLogger")
        self.nodename = self.info.get_config('node', None,
                                             gethostname().split('.')[0])

    #pylint: disable=too-many-branches
    def load_testcases(self, test_module_name, topLevel=False):
        """ load and check test description file """

        rtn = 0
        if not os.path.exists(test_module_name):
            self.logger.error("Description file not found: %s", \
                             test_module_name)
            rtn = 1
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
        if 'passToConfig' not in self.test_info or \
           self.test_info['passToConfig'] is None:
            self.test_info['passToConfig'] = {}
        if 'subList' not in self.test_info or \
           self.test_info['subList'] is None:
            self.test_info['subList'] = {}
        # set test name
        self.test_info['subList']['nodename'] = self.nodename
        self.test_info['subList']['hostlist'] = \
             ",".join(self.info.get_config(keyname='host_list',
                                           default=[self.nodename]))
        fileName = os.path.splitext(os.path.basename(test_module_name))[0]
        moduleName = self.test_info['module'].get('name', "")
        logBaseName = self.test_info['module'].get('logBaseName', "")
        if topLevel:
            self.test_info['testName'] = moduleName
        elif logBaseName:
            if logBaseName != moduleName:
                self.test_info['testName'] = \
                    "{!s}_{!s}".format(logBaseName, moduleName)
            else:
                self.test_info['testName'] = logBaseName
        elif fileName != moduleName:
            self.test_info['testName'] = \
                "{!s}_{!s}".format(fileName, moduleName)
        else:
            self.test_info['testName'] = moduleName

        # create test set name for results file
        if self.info.get_config('node'):
            self.test_info['testSetName'] = "{!s}_{!s}".format(
                self.test_info['testName'], self.nodename)
        else:
            self.test_info['testSetName'] = self.test_info['testName']
        return rtn
    #pylint: enable=too-many-branches

    def nodeName(self):
        """ return the node name """
        return self.nodename

    def cleanup_test_info(self):
        """ post testcase run cleanup """
        self.remove_tmp_dir()
        del self.test_info

    def dump_test_info(self, logdir=""):
        """ dump the test info to the output directory """
        if logdir and os.path.exists(logdir):
            name = os.path.join(logdir,
                                "{!s}.{!s}.test_info_yml.{!s}.log".format(
                                    self.test_info['testSetName'],
                                    self.test_info['testName'],
                                    self.nodename)
                               )
        else:
            name = os.path.join(self.log_dir_base,
                                "{!s}.{!s}.test_info_yml.{!s}.log".format(
                                    self.test_info['testSetName'],
                                    self.test_info['testName'],
                                    self.nodename)
                               )
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
        """ set a key in the test info """
        if subkey:
            if keyname not in self.test_info:
                self.test_info[keyname] = {}
            self.test_info[keyname][subkey] = keyvalue
        else:
            self.test_info[keyname] = keyvalue

    def get_defaultENV(self, keyname=None, default=""):
        """ get a environment """
        if keyname:
            value = self.test_info['defaultENV'].get(keyname, default)
        else:
            value = self.test_info['defaultENV']
        return value

    def set_defaultENV(self, keyname=None, keyvalue=None):
        """ setup the environment """
        self.test_info['defaultENV'][keyname] = keyvalue

    def get_passToConfig(self, keyname=None, default=""):
        """ return information from the passToConfig section """
        if keyname:
            value = self.test_info['passToConfig'].get(keyname, default)
        else:
            value = self.test_info['passToConfig']
        return value

    def set_passToConfig(self, keyname=None, keyvalue=None):
        """ setup the environment """
        self.test_info['passToConfig'][keyname] = keyvalue

    def get_module(self, keyname=None, default=""):
        """ return information from the module section """
        if keyname:
            value = self.test_info['module'].get(keyname, default)
        else:
            value = self.test_info['module']
        return value

    def set_directives(self, keyname=None, value=None):
        """ set a test directive """
        self.test_info['directives'][keyname] = value

    def get_directives(self, keyname=None, default=""):
        """ return the test directives """
        if keyname:
            value = self.test_info['directives'].get(keyname, default)
        else:
            value = self.test_info['directives']
        return value

    def get_execStrategy(self):
        """ return the execStrategy list """
        return self.test_info['execStrategy']

    def parameters_one(self, parameters):
        """ parameters for test strategy """
        self.logger.debug("parameters substitution  %s", str(parameters))
        finditems = re.compile('(?<={)\w+')
        items = finditems.findall(str(parameters))
        #self.logger.error("split %s", str(items))
        for item in items:
            #self.logger.error("item %s", str(item))
            subitem = self.get_defaultENV(item)
            if not subitem:
                what = self.get_test_info('subList', item).split(':')
                #self.logger.error("next %s", str(what))
                if len(what) > 1:
                    if what[0] == 'config':
                        subitem = self.info.get_config(what[1], "")
                    else:
                        subitem = self.get_test_info(what[0], what[1],
                                                     "notfound")
                else:
                    subitem = what[0]
            #self.logger.error("subitem type: %s", str(type(subitem)))
            if not isinstance(subitem, str):
                subitem = ','.join(subitem)
            replace = "{{{!s}}}".format(item)
            #self.logger.error("replace %s with %s", replace, subitem)
            parameters = parameters.replace(replace, str(subitem))
            #self.logger.error("what %s", parameters)

        self.logger.debug("parameters %s", parameters)
        return parameters

    def setup_parameters(self, parameters):
        """ parameters for test strategy """
        if not parameters:
            return parameters
        if '{' in parameters:
            paramList = parameters.split(' ')
            for index in range(len(paramList)):
                if '{' in paramList[index]:
                    paramList[index] = self.parameters_one(paramList[index])
            return ' '.join(paramList)
        return parameters

    def find_item(self, item, where="info"):
        """ keys for test runner """
        #self.logger.error("item %s", str(item))
        what = []
        what = item.split(':')
        #self.logger.error("next %s", str(what))
        if len(what) > 1:
            if where == "info":
                subitem = self.get_test_info(what[0], what[1], "notfound")
            else:
                subitem = self.info.get_config(what[0], what[1], "")
            #self.logger.error("return item %s", str(subitem))
            return {what[1]: subitem}
        else:
            if where == "info":
                self.logger.error("look info %s", str(what))
                subitem = self.get_test_info(what[0], None, "notfound")
            else:
                self.logger.error("look config %s", str(what))
                subitem = self.info.get_config(what[0], None, "")
            #self.logger.error("return item %s", str(subitem))
            return subitem

    def set_configKeys(self, setConfigKeys):
        """ keys for test runner """
        configKeys = {}
        loadFromConfig = setConfigKeys.get('loadFromConfig', "")
        if loadFromConfig:
            for item in loadFromConfig:
                addKeys = self.find_item(item, "config")
                configKeys.update(addKeys)
        loadFromInfo = setConfigKeys.get('loadFromInfo', "")
        if loadFromInfo:
            for item in loadFromInfo:
                InfoKeys = self.find_item(item, "info")
                configKeys.update(InfoKeys)
        return configKeys

#!/usr/bin/env python3
# Copyright (c) 2016-2017 Intel Corporation
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
test runner info class

"""

import os
import json

#pylint: disable=too-many-locals
#pylint: disable=consider-using-enumerate
#pylint: disable=too-many-branches
#pylint: disable=too-many-statements


class InfoRunner():
    """Simple test runner"""
    config = {}
    info = {}

    def __init__(self, config=None):
        self.config = config

    def load_build_vars(self):
        """ load the build_vars file """

        rootpath = os.getcwd()
        print("path: %s" % rootpath)
        opts_file = rootpath + "/.build_vars.json"
        if not os.path.exists(opts_file):
            buildpath = self.config.get('build_path', "")
            opts_file = buildpath + "/.build_vars.json"
            if not os.path.exists(opts_file):
                print("build_vars.json file not found here:\n%s\nor here:\n%s" \
                      % (rootpath, buildpath))
            return 0
        print("use file: %s" % opts_file)
        with open(opts_file, "r") as info_file:
            self.info = json.load(info_file)
        return 1

    def env_setup(self):
        """ setup the environment """
        print("TestRunner: setUp env begin")

        if not self.load_build_vars():
            self.info = {}
            return 0

        print("------------------------------------------------")
        ompi_path = os.path.join(self.info['OMPI_PREFIX'], "bin")
        path = os.getenv("PATH", "")
        path_list = path.split(":")
        index_list = []
        print("TestRunner: original path: %s" % path)
        for path_index in range(len(path_list)):
            if path_list[path_index].find("openmpi") > 0:
                index_list.append(path_index)
        for index in index_list:
            path_list.pop(index)
        if path.find(ompi_path) < 0:
            path_list.insert(0, ompi_path)
        installed_path = self.info['PREFIX']
        test_path = os.path.join(installed_path, "TESTING", "tests")
        if path.find(test_path) < 0:
            path_list.insert(0, test_path)
        bin_path = os.path.join(installed_path, "bin")
        if path.find(bin_path) < 0:
            path_list.insert(0, bin_path)
        newpath = ':'.join(path_list)
        os.environ['PATH'] = newpath
        print("TestRunner: new path: %s" % newpath)
        print("------------------------------------------------")
        installed_libpath = os.path.join(self.info['PREFIX'], "lib")
        ompi_libpath = os.path.join(self.info['OMPI_PREFIX'], "lib")
        libpath = os.getenv("LD_LIBRARY_PATH")
        if libpath:
            libpath_list = libpath.split(":")
            index_list = []
            print("TestRunner: original libpath: %s" % libpath)
            for libpath_index in range(len(libpath_list)):
                if libpath_list[libpath_index].find("openmpi") > 0:
                    index_list.append(libpath_index)
            for index in index_list:
                libpath_list.pop(index)
            if libpath.find(ompi_libpath) < 0:
                libpath_list.insert(0, ompi_libpath)
            if libpath.find(installed_libpath) < 0:
                libpath_list.insert(0, installed_libpath)
        else:
            libpath_list = []
            libpath_list.append(installed_libpath)
            libpath_list.append(ompi_libpath)
        newlibpath = ':'.join(libpath_list)
        os.environ['LD_LIBRARY_PATH'] = newlibpath
        print("TestRunner: new libpath: %s" % newlibpath)
        print("------------------------------------------------\n")
        if os.uname()[0] == "Darwin":
            self.setup_Darwin()
        print("TestRunner: setUp  env end")
        return 1

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

    def get_info(self, keyname=None):
        """ setup the environment """
        return self.info.get(keyname, "")

    def set_info(self, keyname=None, keyvalue=None):
        """ setup the environment """
        self.info[keyname] = keyvalue

    def get_config(self, keyname=None, subkey=None, default=""):
        """ setup the environment """
        if subkey:
            try:
                value = self.config[keyname][subkey]
            except KeyError:
                value = default
        else:
            value = self.config.get(keyname, default)
        return value

    def set_config(self, keyname=None, subkey=None, keyvalue=None):
        """ setup the environment """
        if subkey:
            if keyname not in self.config:
                self.config[keyname] = {}
            self.config[keyname][subkey] = keyvalue
        else:
            self.config[keyname] = keyvalue

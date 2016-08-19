#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
test runner info class

"""

import os
import json


class InfoRunner():
    """Simple test runner"""
    config = {}
    info = {}

    def __init__(self, config=None):
        self.config = config

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
        print("TestRunner: setUp  env end")

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

    def get_config(self, keyname=None, subkey=None):
        """ setup the environment """
        if subkey:
            try:
                value = self.config[keyname][subkey]
            except KeyError:
                value = ""
        else:
            value = self.config.get(keyname, "")
        return value

    def set_config(self, keyname=None, subkey=None, keyvalue=None):
        """ setup the environment """
        if subkey:
            self.config[keyname][subkey] = keyvalue
        else:
            self.config[keyname] = keyvalue

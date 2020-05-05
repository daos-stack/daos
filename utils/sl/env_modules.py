#!/usr/bin/python
# Copyright (c) 2019-2020 Intel Corporation
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
"""Wrapper for Modules so we can load an MPI before builds or tests"""
from __future__ import print_function
import os
import subprocess
try:
    from subprocess import DEVNULL
except ImportError:
    DEVNULL = open(os.devnull, "wb")
from distutils.spawn import find_executable
import re

class _env_module():
    """Class for utilizing Modules component to load environment modules"""
    env_module_init = None
    _mpi_map = {"mpich":['mpi/mpich-x86_64', 'mpich'],
                "openmpi":['mpi/openmpi3-x86_64', 'gnu-openmpi',
                           'mpi/openmpi-x86_64']}

    def __init__(self):
        """Load Modules for initializing envirables"""
        self._module_func = self._init_module_func(lambda *x: False)
        self._module_load = self._init_mpi_module()

    @staticmethod
    def _module_old(*args):
        """invoke module and save environment"""
        cmd = ['/usr/bin/modulecmd', 'python']

        for arg in args:
            cmd.append(arg)
        output = subprocess.check_output(cmd)
        # pylint: disable=exec-used
        exec(output)  #nosec
        # pylint: enable=exec-used

    @staticmethod
    def _setup_old(path_init, default_func):
        if os.environ.get("MODULEPATH", False):
            return _env_module._module_old
        if not path_init or not os.path.exists(path_init):
            return default_func
        with open(path_init, "r") as pfile:
            paths = []
            for line in pfile.readlines():
                line = re.sub(r"#.*$", "", line.strip()).strip()
                if line:
                    paths += line.split(":")
            os.environ["MODULEPATH"] = ":".join(paths)
        return _env_module._module_old

    def _init_module_func(self, default_func):
        """Initialize environment modules"""
        python_init = None
        path_init = None
        for root, _dirs, files in os.walk('/usr/share/Modules'):
            for fname in files:
                if not python_init and fname == "python.py":
                    temp = os.path.join(root, fname)
                    if "init" in temp:
                        python_init = temp
                if not path_init and fname == ".modulespath":
                    path_init = os.path.join(root, fname)
                if path_init and python_init:
                    break

        if python_init is None:
            return default_func

        try:
            subprocess.check_call(['sh', '-l', '-c', 'module -V'], \
                                  stdout=DEVNULL, stderr=DEVNULL) #nosec
        except subprocess.CalledProcessError:
            return self._setup_old(path_init, default_func)

        tmp_globals = {'os': os, 're': re, 'subprocess': subprocess}
        tmp_locals = {}
        try:
            # if successful, this will define module, a function
            # that invokes module on the command line
            # pylint: disable=exec-used
            exec(open(python_init).read(), tmp_globals, tmp_locals)  #nosec
            # pylint: enable=exec-used
        except KeyError:
            return default_func

        module = tmp_locals.get('module', default_func)

        return module

    def _init_mpi_module(self):
        """init mpi module function"""
        try:
            subprocess.check_call(['sh', '-l', '-c', 'module -V'], \
                                  stdout=DEVNULL, stderr=DEVNULL)  #nosec
        except subprocess.CalledProcessError:
            # older version of module return -1
            return self._mpi_module_old

        return self._mpi_module

    def _mpi_module(self, mpi):
        """attempt to load the requested module"""
        load = []
        unload = []

        for key, value in self._mpi_map.items():
            if key == mpi:
                load = value
            else:
                unload += value

        for to_load in load:
            if self._module_func('is-loaded', to_load):
                return True

        for to_unload in unload:
            if self._module_func('is-loaded', to_unload):
                self._module_func('unload', to_unload)

        for to_load in load:
            if self._module_func('load', to_load):
                print("Loaded %s" % to_load)
                return True

        return False

    def _mpi_module_old(self, mpi):
        """attempt to load the requested module"""
        load = []
        for key, value in self._mpi_map.items():
            if key == mpi:
                load = value

        self._module_func('purge')
        for to_load in load:
            self._module_func('load', to_load)
            print("Looking for %s" % to_load)
            if find_executable('mpirun'):
                print("Loaded %s" % to_load)
                return True
        return False

    @staticmethod
    def setup_pkg_config(exe_path):
        """Setup PKG_CONFIG_PATH as not all platforms set it"""
        dirname = os.path.dirname(os.path.dirname(exe_path))
        pkg_path = os.environ.get("PKG_CONFIG_PATH", None)
        pkg_paths = []
        if pkg_path:
            pkg_paths = pkg_path.split(":")
        for path in ["lib", "lib64"]:
            check_path = os.path.join(dirname, path, "pkgconfig")
            if check_path in pkg_paths:
                continue
            if os.path.exists(check_path):
                pkg_paths.append(check_path)
        os.environ["PKG_CONFIG_PATH"] = ":".join(pkg_paths)

    def load_mpi(self, mpi):
        """Invoke the mpi loader"""
        if not self._module_load(mpi):
            print("No %s found\n" % mpi)
            return False
        exe_path = find_executable('mpirun')
        if not exe_path:
            print("No mpirun found in path. Could not configure %s\n" % mpi)
            return False
        self.setup_pkg_config(exe_path)
        return True

    def show_avail(self):
        """list available modules"""
        try:
            if not self._module_func('avail'):
                print("Modules doesn't appear to be installed")
        except subprocess.CalledProcessError:
            print("Could not invoke module avail")

def load_mpi(mpi):
    """global function to load MPI into os.environ"""
    if _env_module.env_module_init is None:
        _env_module.env_module_init = _env_module()
    return _env_module.env_module_init.load_mpi(mpi)

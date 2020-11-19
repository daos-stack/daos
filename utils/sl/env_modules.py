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
import sys
import subprocess
import errno
import distro
from subprocess import PIPE, Popen
from distutils.spawn import find_executable

class _env_module(): # pylint: disable=invalid-name
    """Class for utilizing Modules component to load environment modules"""
    env_module_init = None
    _mpi_map = {"mpich":['mpi/mpich-x86_64', 'gnu-mpich'],
                "openmpi":['mpi/openmpi3-x86_64', 'gnu-openmpi',
                           'mpi/openmpi-x86_64']}

    def __init__(self):
        """Load Modules for initializing envirables"""
        # Leap 15's lmod-lua doesn't include the usual module path
        # in it's MODULEPATH, for some unknown reason
        os.environ["MODULEPATH"] = ":".join([os.path.sep +
                                             os.path.join("usr", "share",
                                                          "modules"),
                                             os.path.sep +
                                             os.path.join("etc",
                                                          "modulefiles")] +
                                            os.environ.get("MODULEPATH",
                                                           "").split(":"))
        self._module_load = self._init_mpi_module()

    def _module_func(self, command, *arguments): # pylint: disable=no-self-use
        num_args = len(arguments)
        cmd = ['/usr/share/lmod/lmod/libexec/lmod', 'python', command]
        if num_args == 1:
            cmd += arguments[0].split()
        else:
            cmd += list(arguments)

        try:
            proc = Popen(cmd, stdout=PIPE, stderr=PIPE)
        except OSError as error:
            if error.errno == errno.ENOENT:
                return None, None
        except FileNotFoundError:
            return None, None

        stdout, stderr = proc.communicate()

        if sys.version_info[0] > 2:
            ns = {}
            exec(stdout.decode(), ns) # pylint: disable=exec-used

            return ns['_mlstatus'], stderr.decode()
        else:
            exec(stdout.decode()) # pylint: disable=exec-used

            return _mlstatus, stderr.decode() # pylint: disable=undefined-variable


    def _init_mpi_module(self):
        """init mpi module function"""

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
            if self._module_func('is-loaded', to_load)[0]:
                return True

        for to_unload in unload:
            if self._module_func('is-loaded', to_unload)[0]:
                self._module_func('unload', to_unload)

        for to_load in load:
            if self._module_func('is-avail', to_load)[0] and \
               self._module_func('load', to_load)[0]:
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
            if not self._module_func('avail')[0]:
                print("Modules doesn't appear to be installed")
        except subprocess.CalledProcessError:
            print("Could not invoke module avail")

def load_mpi(mpi):
    """global function to load MPI into os.environ"""
    # On Ubuntu, MPI stacks use alternatives and need root to change their
    # pointer, so just verify that the desired MPI is loaded
    if distro.id() == "ubuntu":
        try:
            proc = Popen(['update-alternatives', '--query', 'mpi'], stdout=PIPE)
        except OSError as error:
            print("Error running update-alternatives")
            if error.errno == errno.ENOENT:
                return False
        for line in proc.stdout.readlines():
            if line.startswith(b"Value:"):
                if line[line.rfind(b".")+1:-1].decode() == mpi:
                    return True
                return False
        return False

    if _env_module.env_module_init is None:
        _env_module.env_module_init = _env_module()
    return _env_module.env_module_init.load_mpi(mpi)

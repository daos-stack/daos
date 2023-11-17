"""SCons.Tool.doneapi

Hack to support oneapi version of Intel compilers

"""
import os
import sys

import SCons.Errors
import SCons.Tool.gcc
import SCons.Util
import SCons.Warnings


# pylint: disable=too-few-public-methods
class DetectCompiler():
    """Find oneapi compiler"""

    def __init__(self):
        root = '/opt/intel/oneapi/compiler/latest'
        binp = os.path.join(root, 'linux', 'bin')
        libp = os.path.join(root, 'linux', 'lib')
        include = os.path.join(root, 'linux', 'include')
        binarch = os.path.join(binp, 'intel64')
        libarch = os.path.join(root, 'linux', 'compiler', 'lib', 'intel64_lin')
        icx = os.path.join(binp, 'icx')
        self.map = {}
        sys.stdout.flush()
        for path in [root, binp, libp, binarch, libarch, include, icx]:
            if not os.path.exists(path):
                return
        self.map = {'root': root,
                    'bin': binp,
                    'lib': libp,
                    'binarch': binarch,
                    'libarch': libarch,
                    'include': include,
                    'icx': icx}

    def __getitem__(self, key):
        """Return key"""
        return self.map.get(key, None)


def generate(env):
    """Add Builders and construction variables for Intel Oneapi C++C++ compiler."""
    SCons.Tool.gcc.generate(env)

    detector = DetectCompiler()
    if detector['icx'] is None:
        raise SCons.Errors.InternalError("No oneapi compiler found")

    env['INTEL_C_COMPILER_TOP'] = detector['root']
    paths = {'INCLUDE': 'include',
             'LIB': 'libarch',
             'PATH': 'binarch',
             'LD_LIBRARY_PATH': 'libarch'}
    for (key, value) in paths.items():
        env.PrependENVPath(key, detector[value])
    env.PrependENVPath("PATH", detector["bin"])
    env.PrependENVPath("LIB", detector["lib"])
    env.PrependENVPath("LD_LIBRARY_PATH", detector["lib"])
    env['CC'] = 'icx'
    env['CXX'] = 'icpx'
    env['AR'] = 'ar'
    env['LD'] = 'xild'  # not used by default


def exists(_env):
    """Find if icx exists"""
    detector = DetectCompiler()
    if detector['icx'] is None:
        return False
    return True

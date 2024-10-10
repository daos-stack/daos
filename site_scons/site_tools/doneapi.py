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
        binp = os.path.join(root, 'bin')
        libp = os.path.join(root, 'lib')
        include = os.path.join(root, 'include')
        icx = os.path.join(binp, 'icx')
        self.map = {}
        sys.stdout.flush()
        for path in [root, binp, libp, include, icx]:
            if not os.path.exists(path):
                print(f"oneapi compiler: {path} doesn't exist")
                return
        self.map = {'root': root,
                    'bin': binp,
                    'lib': libp,
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
             'PATH': 'bin',
             'LD_LIBRARY_PATH': 'lib'}
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

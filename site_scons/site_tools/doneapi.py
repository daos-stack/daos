"""SCons.Tool.doneapi

Hack to support oneapi version of Intel compilers

"""
import sys, os

is_linux = sys.platform.startswith('linux')

if is_linux:
    import SCons.Tool.gcc
import SCons.Util
import SCons.Warnings
import SCons.Errors

if not is_linux:
    raise SCons.Errors.UserError("This tool doesn't work on non-Linux platforms")

class OneapiError(SCons.Errors.InternalError):
    pass

class DetectCompiler(object):

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
        if not os.path.exists(root) or \
           not os.path.exists(binp) or \
           not os.path.exists(libp) or \
           not os.path.exists(binarch) or \
           not os.path.exists(libarch) or \
           not os.path.exists(include) or \
           not os.path.exists(icx):
            return
        self.map = {'root' : root,
                    'bin' : binp,
                    'lib' : libp,
                    'binarch' : binarch,
                    'libarch' : libarch,
                    'include' : include,
                    'icx' : icx}

    def __getitem__(self, key):
        """Return key"""
        return self.map.get(key, None)

def generate(env):
    """Add Builders and construction variables for Intel Oneapi C++C++ compiler
    to an Environment.
    """
    SCons.Tool.gcc.generate(env)

    detector = DetectCompiler()
    if detector['icx'] is None:
        raise OneapiError("No oneapi compiler found")

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
    env['AR'] = 'xiar'
    env['LD'] = 'xild'  # not used by default

def exists(env):
    detector = DetectCompiler()
    if detector['icx'] is None:
        return False
    return True

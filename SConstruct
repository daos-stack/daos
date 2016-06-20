"""SConstruct to build all components"""
import sys
import os
sys.path.insert(0, os.path.realpath("."))
from prereq_tools import PreReqComponent

def scons():
    """Build requested prerequisite components"""
    env = DefaultEnvironment()

    opts = Variables()
    reqs = PreReqComponent(env, opts)
    reqs.preload("components.py")

    print reqs.get_defined_components()
    opts.Add(ListVariable("REQUIRES",
                          "List of libraries to build",
                          'mercury,ompi',
                          reqs.get_defined_components()))
    opts.Update(env)

    reqs.require(env, *env.get("REQUIRES"))

    build_info = reqs.get_build_info()

    build_info.gen_script(".build_vars.sh")

    try:
        Help(opts.GenerateHelpText(env), append=True)
    except TypeError:
        Help(opts.GenerateHelpText(env))

if __name__ == 'SCons.Script':
    scons()

"""Common DAOS build functions"""
from SCons.Script import Literal

def library(env, *args, **kwargs):
    """build SharedLibrary with relative RPATH"""
    denv = env.Clone()
    denv.AppendUnique(RPATH=[Literal(r'\$$ORIGIN')])
    return denv.SharedLibrary(*args, **kwargs)

def program(env, *args, **kwargs):
    """build Program with relative RPATH"""
    denv = env.Clone()
    denv.AppendUnique(RPATH=[Literal(r'\$$ORIGIN/../lib64')])
    return denv.Program(*args, **kwargs)

def test(env, *args, **kwargs):
    """build Program with fixed RPATH"""
    denv = env.Clone()
    denv.AppendUnique(RPATH=["$PREFIX/lib64"])
    return denv.Program(*args, **kwargs)

def install(env, subdir, files):
    """install file to the subdir"""
    denv = env.Clone()
    path = "$PREFIX/%s" % subdir
    denv.Install(path, files)

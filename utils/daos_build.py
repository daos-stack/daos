"""Common DAOS build functions"""
from SCons.Script import Literal
from env_modules import load_mpi
from distutils.spawn import find_executable
import os

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

def load_mpi_path(env):
    """Load location of mpicc into path if MPI_PKG is set"""
    mpicc = find_executable("mpicc")
    if mpicc:
        env.AppendENVPath("PATH", os.path.dirname(mpicc))

def _configure_mpi_pkg(env, libs):
    """Configure MPI using pkg-config"""
    mpicc = find_executable("mpicc")
    if mpicc:
        env.Replace(CC="mpicc")
        env.Replace(LINK="mpicc")
        return env.subst("$MPI_PKG")
    try:
        env.ParseConfig("pkg-config --cflags --libs $MPI_PKG")
    except OSError as e:
        print("\n**********************************")
        print("Could not find package MPI_PKG=%s\n" % env.subst("$MPI_PKG"))
        print("Unset it or update PKG_CONFIG_PATH")
        print("**********************************")
        raise e

    # assume mpi is needed in the fallback case
    libs.append('mpi')
    return env.subst("$MPI_PKG")

def configure_mpi(prereqs, env, libs, required=None):
    """Check if mpi exists and configure environment"""
    if env.subst("$MPI_PKG") != "":
        return _configure_mpi_pkg(env, libs)

    mpis = ['openmpi', 'mpich']
    if not required is None:
        if isinstance(required, str):
            mpis = [required]
        else:
            mpis = required

    for mpi in mpis:
        load_mpi(mpi)
        comp = mpi
        if mpi == "openmpi":
            comp = "ompi"
        if prereqs.check_component(comp):
            prereqs.require(env, comp)
            print("%s is installed" % mpi)
            libs.append('mpi')
            return comp
        print("No %s installed and/or loaded" % mpi)
    print("No OMPI installed")
    return None

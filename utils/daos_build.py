"""Common DAOS build functions"""
from SCons.Script import Literal
from env_modules import load_mpi

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

def _configure_mpi_pkg(env):
    """Configure MPI using pkg-config"""
    try:
        env.ParseConfig("pkg-config --cflags --libs $MPI_PKG")
    except OSError as e:
        print("\n**********************************")
        print("Could not find package MPI_PKG=%s\n" % env.subst("$MPI_PKG"))
        print("Unset it or update PKG_CONFIG_PATH")
        print("**********************************")
        raise e

    return env.subst("$MPI_PKG")

def configure_mpi(prereqs, env, required=None):
    """Check if mpi exists and configure environment"""
    if env.subst("$MPI_PKG") != "":
        return _configure_mpi_pkg(env)

    mpis = ['ompi', 'mpich']
    if not required is None:
        if isinstance(required, str):
            mpis = [required]
        else:
            mpis = required

    for mpi in mpis:
        load_mpi(mpi)
        if prereqs.check_component(mpi):
            prereqs.require(env, mpi)
            print("%s is installed" % mpi)
            return mpi
        print("No %s installed and/or loaded" % mpi)
    print("No OMPI installed")
    return None

"""Common DAOS build functions"""
import os

from SCons.Subst import Literal
from SCons.Script import Dir
from SCons.Script import GetOption
from SCons.Script import WhereIs
from env_modules import load_mpi
import compiler_setup


class DaosLiteral(Literal):
    """A wrapper for a Literal."""
    # pylint: disable=too-few-public-methods

    def __hash__(self):
        """Workaround for missing hash function"""
        return hash(self.lstr)


def add_rpaths(env, install_off, set_cgo_ld, is_bin):
    """Add relative rpath entries"""
    if GetOption('no_rpath'):
        if set_cgo_ld:
            env.AppendENVPath("CGO_LDFLAGS", env.subst("$_LIBDIRFLAGS "),
                              sep=" ")
        return
    env.AppendUnique(RPATH_FULL=['$PREFIX/lib64'])
    rpaths = env.subst("$RPATH_FULL").split()
    prefix = env.get("PREFIX")
    if not is_bin:
        path = r'\$$ORIGIN'
        env.AppendUnique(RPATH=[DaosLiteral(path)])
    for rpath in rpaths:
        if rpath.startswith('/usr'):
            env.AppendUnique(RPATH=[rpath])
            continue
        if install_off is None:
            env.AppendUnique(RPATH=[os.path.join(prefix, rpath)])
            continue
        relpath = os.path.relpath(rpath, prefix)
        if relpath != rpath:
            joined = os.path.normpath(os.path.join(install_off, relpath))
            path = r'\$$ORIGIN/%s' % (joined)
            if set_cgo_ld:
                env.AppendENVPath("CGO_LDFLAGS", "-Wl,-rpath=$ORIGIN/%s/%s" %
                                  (install_off, relpath), sep=" ")
            else:
                env.AppendUnique(RPATH=[DaosLiteral(path)])
    for rpath in rpaths:
        path = os.path.join(prefix, rpath)
        if is_bin:
            # NB: Also use full path so intermediate linking works
            env.AppendUnique(LINKFLAGS=["-Wl,-rpath-link=%s" % path])
        else:
            # NB: Also use full path so intermediate linking works
            env.AppendUnique(RPATH=[path])

    if set_cgo_ld:
        env.AppendENVPath("CGO_LDFLAGS",
                          env.subst("$_LIBDIRFLAGS " "$_RPATH"),
                          sep=" ")


def add_build_rpath(env, pathin="."):
    """Add a build directory with -Wl,-rpath-link"""
    path = Dir(pathin).path
    env.AppendUnique(LINKFLAGS=["-Wl,-rpath-link=%s" % path])
    env.AppendENVPath("CGO_LDFLAGS", "-Wl,-rpath-link=%s" % path, sep=" ")
    # We actually run installed binaries from the build area to generate
    # man pages.  In such cases, we need LD_LIBRARY_PATH set to pick up
    # the dependencies
    env.AppendENVPath("LD_LIBRARY_PATH", path)


def library(env, *args, **kwargs):
    """build SharedLibrary with relative RPATH"""
    denv = env.Clone()
    denv.Replace(RPATH=[])
    add_rpaths(denv, kwargs.get('install_off', '..'), False, False)
    return denv.SharedLibrary(*args, **kwargs)


def program(env, *args, **kwargs):
    """build Program with relative RPATH"""
    denv = env.Clone()
    denv.Replace(RPATH=[])
    add_rpaths(denv, kwargs.get('install_off', '..'), False, True)
    return denv.Program(*args, **kwargs)


def test(env, *args, **kwargs):
    """build Program with fixed RPATH"""
    denv = env.Clone()
    denv.Replace(RPATH=[])
    add_rpaths(denv, kwargs.get("install_off", None), False, True)
    return denv.Program(*args, **kwargs)


def install(env, subdir, files):
    """install file to the subdir"""
    denv = env.Clone()
    path = "$PREFIX/%s" % subdir
    denv.Install(path, files)


def _find_mpicc(env):
    """find mpicc"""

    mpicc = WhereIs('mpicc')
    if not mpicc:
        return False

    env.Replace(CC="mpicc")
    env.Replace(LINK="mpicc")
    env.PrependENVPath('PATH', os.path.dirname(mpicc))
    compiler_setup.base_setup(env)

    return True


def _configure_mpi_pkg(env):
    """Configure MPI using pkg-config"""
    if _find_mpicc(env):
        return True
    try:
        env.ParseConfig("pkg-config --cflags --libs $MPI_PKG")
    except OSError as e:
        print("\n**********************************")
        print("Could not find package MPI_PKG=%s\n" % env.subst("$MPI_PKG"))
        print("Unset it or update PKG_CONFIG_PATH")
        print("**********************************")
        raise e

    return True


def configure_mpi(env):
    """Check if mpi exists and configure environment"""

    if GetOption('help'):
        return True

    env['CXX'] = None

    if env.subst("$MPI_PKG") != "":
        return _configure_mpi_pkg(env)

    for mpi in ['openmpi', 'mpich']:
        if not load_mpi(mpi):
            continue
        if _find_mpicc(env):
            print("%s is installed" % mpi)
            return True
        print("No %s installed and/or loaded" % mpi)
    print("No MPI installed")
    return False

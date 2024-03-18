"""Common DAOS build functions"""
import os

from env_modules import load_mpi
from SCons.Script import Depends, Dir, Exit, GetOption, WhereIs
from SCons.Subst import Literal

libraries = {}
missing = set()


class DaosLiteral(Literal):
    """A wrapper for a Literal."""

    # pylint: disable=too-few-public-methods

    def __hash__(self):
        """Workaround for missing hash function"""
        return hash(self.lstr)


def _add_rpaths(env, install_off, set_cgo_ld, is_bin):
    """Add relative rpath entries"""
    if GetOption('no_rpath'):
        if set_cgo_ld:
            env.AppendENVPath("CGO_LDFLAGS", env.subst("$_LIBDIRFLAGS "), sep=" ")
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
            if set_cgo_ld:
                env.AppendENVPath("CGO_LDFLAGS", f'-Wl,-rpath=$ORIGIN/{install_off}/{relpath}',
                                  sep=" ")
            else:
                joined = os.path.normpath(os.path.join(install_off, relpath))
                env.AppendUnique(RPATH=[DaosLiteral(fr'\$$ORIGIN/{joined}')])
    for rpath in rpaths:
        path = os.path.join(prefix, rpath)
        if is_bin:
            # NB: Also use full path so intermediate linking works
            env.AppendUnique(LINKFLAGS=[f'-Wl,-rpath-link={path}'])
        else:
            # NB: Also use full path so intermediate linking works
            env.AppendUnique(RPATH=[path])

    if set_cgo_ld:
        env.AppendENVPath("CGO_LDFLAGS", env.subst("$_LIBDIRFLAGS $_RPATH"), sep=" ")


def _add_build_rpath(env, pathin="."):
    """Add a build directory to rpath"""
    path = Dir(pathin).path
    env.AppendUnique(LINKFLAGS=[f'-Wl,-rpath-link={path}'])
    env.AppendENVPath('CGO_LDFLAGS', f'-Wl,-rpath-link={path}', sep=' ')
    # We actually run installed binaries from the build area to generate
    # man pages.  In such cases, we need LD_LIBRARY_PATH set to pick up
    # the dependencies
    env.AppendENVPath("LD_LIBRARY_PATH", path)


def _known_deps(env, **kwargs):
    """Get list of known libraries

    SCons is sensitive to dependency order so return a consistent order here
    """
    shared_libs = []
    static_libs = []
    if 'LIBS' in kwargs:
        libs = set(kwargs['LIBS'])
    else:
        libs = set(env.get('LIBS', []))

    known_libs = libs.intersection(set(libraries.keys()))
    missing.update(libs - known_libs)
    for item in sorted(known_libs):
        shared = libraries[item].get('shared', None)
        if shared is not None:
            shared_libs.append(shared)
            continue
        static_libs.append(libraries[item].get('static'))
    return (static_libs, shared_libs)


def _get_libname(*args, **kwargs):
    """Work out the basic library name from library builder args"""
    if 'target' in kwargs:
        libname = os.path.basename(kwargs['target'])
    else:
        libname = os.path.basename(args[0])
    if libname.startswith('lib'):
        libname = libname[3:]
    return libname


def _add_lib(libtype, libname, target):
    """Add library to our db"""
    if libname in missing:
        print(f"Detected that build of {libname} happened after use")
        Exit(1)
    if libname not in libraries:
        libraries[libname] = {}
    libraries[libname][libtype] = target


def _run_command(env, target, sources, daos_libs, command):
    """Run Command builder"""
    static_deps, shared_deps = _known_deps(env, LIBS=daos_libs)
    result = env.Command(target, sources + static_deps + shared_deps, command)
    return result


def _static_library(env, *args, **kwargs):
    """Build SharedLibrary with relative RPATH"""
    libname = _get_libname(*args, **kwargs)
    if 'hide_syms' in kwargs:
        # Allow for auto-hiding of symbols, used for the Interception library.  There are multiple
        # ways to do this but for simplicity if hide_syms is used force the use of target,source
        # kwargs rather than args.
        assert not args
        del kwargs['hide_syms']
        real_target = kwargs['target']
        kwargs['target'] = f"{real_target}_source"
    else:
        real_target = None
    lib = env.StaticLibrary(*args, **kwargs)
    if real_target:
        lib = env.Command(real_target, lib, 'objcopy --localize-hidden $SOURCE $TARGET')
    libname = _get_libname(*args, **kwargs)
    _add_lib('static', libname, lib)
    static_deps, shared_deps = _known_deps(env, **kwargs)
    Depends(lib, static_deps)
    env.Requires(lib, shared_deps)
    return lib


def _library(env, *args, **kwargs):
    """Build SharedLibrary with relative RPATH"""
    denv = env.Clone()
    denv.Replace(RPATH=[])
    _add_rpaths(denv, kwargs.get('install_off', '..'), False, False)
    lib = denv.SharedLibrary(*args, **kwargs)
    libname = _get_libname(*args, **kwargs)
    _add_lib('shared', libname, lib)
    static_deps, shared_deps = _known_deps(denv, **kwargs)
    Depends(lib, static_deps)
    env.Requires(lib, shared_deps)
    return lib


def _program(env, *args, **kwargs):
    """Build Program with relative RPATH"""
    denv = env.Clone()
    denv.AppendUnique(LINKFLAGS=['-pie'])
    denv.Replace(RPATH=[])
    _add_rpaths(denv, kwargs.get('install_off', '..'), False, True)
    prog = denv.Program(*args, **kwargs)
    static_deps, shared_deps = _known_deps(env, **kwargs)
    Depends(prog, static_deps)
    env.Requires(prog, shared_deps)
    return prog


def _test_program(env, *args, **kwargs):
    """Build Program with fixed RPATH"""
    denv = env.Clone()
    denv.AppendUnique(LINKFLAGS=['-pie'])
    denv.Replace(RPATH=[])
    _add_rpaths(denv, kwargs.get("install_off", None), False, True)
    testbuild = denv.Program(*args, **kwargs)
    static_deps, shared_deps = _known_deps(env, **kwargs)
    Depends(testbuild, static_deps)
    env.Requires(testbuild, shared_deps)
    return testbuild


def _find_mpicc(env):
    """Find mpicc"""
    mpicc = WhereIs('mpicc')
    if not mpicc:
        return False

    env.Replace(CC="mpicc")
    env.Replace(LINK="mpicc")
    env.PrependENVPath('PATH', os.path.dirname(mpicc))
    env.compiler_setup()

    return True


def _configure_mpi_pkg(env):
    """Configure MPI using pkg-config"""
    if _find_mpicc(env):
        return
    try:
        env.ParseConfig('pkg-config --cflags --libs $MPI_PKG')
    except OSError as error:
        print('\n**********************************')
        print(f'Could not find package MPI_PKG={env.subst("$MPI_PKG")}\n')
        print('Unset it or update PKG_CONFIG_PATH')
        print('**********************************')
        raise error

    return


def _configure_mpi(self):
    """Check if mpi exists and configure environment"""
    if GetOption('help'):
        return None

    def _print(msg):
        if not GetOption('silent'):
            print(msg)

    env = self.Clone()

    env['CXX'] = None

    if env.subst("$MPI_PKG") != "":
        _configure_mpi_pkg(env)
        return env

    for mpi in ['openmpi', 'mpich']:
        if not load_mpi(mpi, GetOption('silent')):
            continue
        if _find_mpicc(env):
            _print(f'{mpi} is installed')
            return env
        _print(f'No {mpi} installed and/or loaded')
    _print("No MPI installed")
    return None


def generate(env):
    """Add daos specific methods to environment"""
    env.AddMethod(_add_build_rpath, 'd_add_build_rpath')
    env.AddMethod(_configure_mpi, 'd_configure_mpi')
    env.AddMethod(_run_command, 'd_run_command')
    env.AddMethod(_add_rpaths, 'd_add_rpaths')
    env.AddMethod(_program, 'd_program')
    env.AddMethod(_test_program, 'd_test_program')
    env.AddMethod(_library, 'd_library')
    env.AddMethod(_static_library, 'd_static_library')


def exists(_env):
    """Tell SCons we exist"""
    return True

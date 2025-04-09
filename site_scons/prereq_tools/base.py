# Copyright 2016-2024 Intel Corporation
# Copyright 2025 Google LLC
# Copyright 2025 Hewlett Packard Enterprise Development LP
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
# -*- coding: utf-8 -*-
"""Classes for building external prerequisite components"""

import configparser
import datetime
import errno
import json
# pylint: disable=too-many-lines
import os
import shutil
import subprocess  # nosec
import sys
import traceback
from copy import deepcopy

from SCons.Errors import InternalError
from SCons.Script import BUILD_TARGETS, Dir, Exit, GetOption, SetOption, WhereIs
from SCons.Variables import BoolVariable, EnumVariable, ListVariable, PathVariable


class DownloadFailure(Exception):
    """Exception raised when source can't be downloaded

    Attributes:
        repo      -- Specified location
        component -- Component
    """

    def __init__(self, repo, component):
        super().__init__()
        self.repo = repo
        self.component = component

    def __str__(self):
        """Exception string"""
        return f'Failed to get {self.component} from {self.repo}'


class BadScript(Exception):
    """Exception raised when a preload script has errors

    Attributes:
        script -- path to script
        trace  -- traceback
    """

    def __init__(self, script, trace):
        super().__init__()
        self.script = script
        self.trace = trace

    def __str__(self):
        """Exception string"""
        return f'Failed to execute {self.script}:\n{self.script} {self.trace}\n\nTraceback'


class MissingDefinition(Exception):
    """Exception raised when component has no definition

    Attributes:
        component    -- component that is missing definition
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """Exception string"""
        return f'No definition for {self.component}'


class BuildFailure(Exception):
    """Exception raised when component fails to build

    Attributes:
        component    -- component that failed to build
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """Exception string"""
        return f'{self.component} failed to build'


class MissingTargets(Exception):
    """Exception raised when expected targets missing after component build

    Attributes:
        component    -- component that has missing targets
    """

    def __init__(self, component, package):
        super().__init__()
        self.component = component
        self.package = package

    def __str__(self):
        """Exception string"""
        if self.package is None:
            return f'{self.component} has missing targets after build.  See config.log for details'
        return f'Package {self.package} is required. Check config.log'


class MissingSystemLibs(Exception):
    """Exception raised when libraries required for target build are not met

    Attributes:
        component    -- component that has missing targets
    """

    def __init__(self, component, prog):
        super().__init__()
        self.component = component
        self.prog = prog

    def __str__(self):
        """Exception string"""
        return f"{self.component} requires {self.prog} for build"


class DownloadRequired(Exception):
    """Exception raised when component is missing but downloads aren't enabled

    Attributes:
        component    -- component that is missing
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """Exception string"""
        return f'{self.component} needs to be built, use --build-deps=yes'


class BuildRequired(Exception):
    """Exception raised when component is missing but builds aren't enabled

    Attributes:
        component    -- component that is missing
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """Exception string"""
        return f'{self.component} needs to be built, use --build-deps=yes'


class RunnerResult():
    """Helper class for Runner that allows returning extra values without changing the API"""

    # pylint: disable=too-few-public-methods
    def __init__(self, rc):
        self.rc = rc

    def __bool__(self):
        """Add a truth function"""
        return self.rc == 0


class Runner():
    """Runs commands in a specified environment"""

    def __init__(self):
        self.env = None
        self.__dry_run = False

    def initialize(self, env):
        """Initialize the environment in the runner"""
        self.env = env
        self.__dry_run = env.GetOption('no_exec')

    def run_commands(self, commands, subdir=None, env=None):
        """Runs a set of commands in specified directory

        Returns a RunnerResult object that resolves to True on process failure.
        """
        # Check that PreReqComponent is initialized
        assert self.env
        retval = RunnerResult(0)

        passed_env = env or self.env

        if subdir:
            print(f'Running commands in {subdir}')
        for command in commands:
            cmd = []
            for part in command:
                if part == 'make':
                    cmd.extend(['make', '-j', str(GetOption('num_jobs'))])
                else:
                    cmd.append(self.env.subst(part))
            if self.__dry_run:
                print(f"Would RUN: {' '.join(cmd)}")
            else:
                print(f"RUN: {' '.join(cmd)}")
                rc = subprocess.call(cmd, shell=False, cwd=subdir, env=passed_env['ENV'])
                if rc != 0:
                    retval = RunnerResult(rc)
                    break
        return retval


RUNNER = Runner()


def default_libpath():
    """On debian systems, the default library path can be queried"""
    if not os.path.isfile('/etc/debian_version'):
        return []
    dpkgarchitecture = WhereIs('dpkg-architecture')
    if not dpkgarchitecture:
        print('No dpkg-architecture found in path.')
        return []
    try:
        # pylint: disable=consider-using-with
        pipe = subprocess.Popen([dpkgarchitecture, '-qDEB_HOST_MULTIARCH'],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        (stdo, _) = pipe.communicate()
        if pipe.returncode == 0:
            archpath = stdo.decode().strip()
            return ['lib/' + archpath]
    except Exception:  # pylint: disable=broad-except
        print('default_libpath, Exception: subprocess.Popen dpkg-architecture')
    return []


class CopyRetriever():
    """Copy from git modules area or specified directory"""

    # pylint: disable=too-few-public-methods
    def __init__(self, source=None, patched=False):
        self.source = source
        self.patched = patched

    def _apply_patches(self, subdir, patches):
        """apply a patch"""
        if self.patched:
            return
        if patches is not None:
            for patch in patches.keys():
                print(f'Applying patch {patch}')
                filter_patch = ['sed', '-i', '/^[di].*/d', patch]
                command = ['patch', '-N', '-p1']
                if patches[patch] is not None:
                    command.extend(['--directory', patches[patch]])
                command.append(f'--input={patch}')
                if not RUNNER.run_commands([filter_patch, command], subdir=subdir):
                    print('Patch may already be applied')

    def get(self, name, subdir, *_args, **kw):
        """Downloads sources from a git repository into subdir"""
        if self.source is None:
            self.source = os.path.join(Dir('#').srcnode().abspath, "src", "external", name)
        print(f'Copying source for {name} from {self.source} to {subdir}')
        exclude = set([".git", ".github"])
        for root, dirs, files in os.walk(self.source, topdown=True):
            dirs[:] = [d for d in dirs if d not in exclude]
            dest_root = root.replace(self.source, subdir)
            print(f"Copying to {dest_root}")
            os.makedirs(dest_root, exist_ok=True)
            for filename in files:
                shutil.copy(os.path.join(root, filename), os.path.join(dest_root, filename))
        self._apply_patches(subdir, kw.get("patches", {}))


class GitRepoRetriever():
    """Identify a git repository from which to download sources"""

    def __init__(self, has_submodules=False, branch=None):

        self.url = None
        self.has_submodules = has_submodules
        self.branch = branch
        self.commit_sha = None

    def checkout_commit(self, subdir):
        """Checkout a certain commit SHA or branch"""
        if self.commit_sha is not None:
            commands = [['git', 'checkout', self.commit_sha]]
            if not RUNNER.run_commands(commands, subdir=subdir):
                raise DownloadFailure(self.url, subdir)

    def _apply_patches(self, subdir, patches):
        """git-apply a certain hash"""
        if patches is not None:
            for patch in patches.keys():
                print(f'Applying patch {patch}')
                command = ['git', 'apply']
                if patches[patch] is not None:
                    command.extend(['--directory', patches[patch]])
                command.append(patch)
                if not RUNNER.run_commands([command], subdir=subdir):
                    raise DownloadFailure(self.url, subdir)

    def _update_submodules(self, subdir):
        """Update the git submodules"""
        if self.has_submodules:
            commands = [['git', 'submodule', 'init'], ['git', 'submodule', 'update']]
            if not RUNNER.run_commands(commands, subdir=subdir):
                raise DownloadFailure(self.url, subdir)

    def get(self, name, subdir, repo, **kw):
        """Downloads sources from a git repository into subdir"""
        # Now checkout the commit_sha if specified
        print(f'Downloading source for {name}')
        self.url = repo
        passed_commit_sha = kw.get("commit_sha", None)
        if passed_commit_sha is None:
            comp = os.path.basename(subdir)
            print(f"""
*********************** ERROR ************************
No commit_versions entry in utils/build.config for
{comp}. Please specify one to avoid breaking the
build with random upstream changes.
*********************** ERROR ************************\n""")
            raise DownloadFailure(self.url, subdir)

        if not os.path.exists(subdir):
            commands = [['git', 'clone', self.url, subdir]]
            if not RUNNER.run_commands(commands):
                raise DownloadFailure(self.url, subdir)
        self._get_specific(subdir, **kw)

    def _get_specific(self, subdir, **kw):
        """Checkout the configured commit"""
        # If the config overrides the branch, use it.  If a branch is
        # specified, check it out first.
        branch = kw.get("branch", None)
        if branch is None:
            branch = self.branch
        self.branch = branch
        if self.branch:
            command = [['git', 'checkout', branch]]
            if not RUNNER.run_commands(command, subdir=subdir):
                command = [['git', 'fetch', '-t', '-a']]
                if not RUNNER.run_commands(command, subdir=subdir):
                    raise DownloadFailure(self.url, subdir)
            self.commit_sha = self.branch
            self.checkout_commit(subdir)

        # Now checkout the commit_sha if specified
        passed_commit_sha = kw.get("commit_sha", None)
        if passed_commit_sha is not None:
            command = [['git', 'checkout', passed_commit_sha]]
            if not RUNNER.run_commands(command, subdir=subdir):
                command = [['git', 'fetch', '-t', '-a']]
                if not RUNNER.run_commands(command, subdir=subdir):
                    raise DownloadFailure(self.url, subdir)
            self.commit_sha = passed_commit_sha
            self.checkout_commit(subdir)

        # reset patched diff
        command = [['git', 'reset', '--hard', 'HEAD']]
        if not RUNNER.run_commands(command, subdir=subdir):
            raise DownloadFailure(self.url, subdir)
        self._update_submodules(subdir)
        # Now apply any patches specified
        self._apply_patches(subdir, kw.get("patches", {}))


class BuildInfo():
    """A utility class to save build information"""

    def __init__(self):
        self.info = {}

    def update(self, var, value):
        """Save a variable in the build info"""
        self.info[var] = value

    def save(self, filename):
        """Create a file to store path information for a build"""
        with open(filename, "w") as build_info:
            json.dump(self.info, build_info, skipkeys=True, indent=2)

    def gen_script(self, script_name):
        """Generate a shell script to set PATH, LD_LIBRARY_PATH, and PREFIX variables"""
        with open(script_name, "w") as script:
            script.write('# Automatically generated by'
                         + f' {sys.argv[0]} at {datetime.datetime.today()}\n\n')

            lib_paths = []
            paths = []
            components = []

            for var in sorted(self.info.keys()):
                if not isinstance(self.info[var], str):
                    continue
                if "PREFIX" not in var:
                    continue
                if self.info[var] == "/usr":
                    continue
                script.write(f'SL_{var}={self.info[var]}\n')
                components.append(var)
                path = os.path.join(self.info[var], "bin")
                lib = os.path.join(self.info[var], "lib")
                lib64 = os.path.join(self.info[var], "lib64")
                if os.path.exists(path) and path not in paths:
                    paths.insert(0, path)
                if os.path.exists(lib) and lib not in lib_paths:
                    lib_paths.insert(0, lib)
                if os.path.exists(lib64) and lib64 not in lib_paths:
                    lib_paths.insert(0, lib64)
            script.write(f'SL_LD_LIBRARY_PATH={os.pathsep.join(lib_paths)}\n')
            script.write(f'SL_PATH={os.pathsep.join(paths)}\n')
            component_list = ' '.join(components)
            script.write(f'SL_COMPONENTS="{component_list}"\n')
            script.write(f'SL_BUILD_DIR={self.info["BUILD_DIR"]}\n')
            script.write(f'SL_SRC_DIR={os.getcwd()}\n')


def ensure_dir_exists(dirname, dry_run):
    """Ensure a directory exists"""
    if not os.path.exists(dirname):
        if dry_run:
            print(f'Would create {dry_run}')
            return
        try:
            os.makedirs(dirname)
        except Exception as error:  # pylint: disable=broad-except
            if not os.path.isdir(dirname):
                raise error

    if not os.path.isdir(dirname):
        raise IOError(errno.ENOTDIR, 'Not a directory', dirname)


# pylint: disable-next=function-redefined
class PreReqComponent():
    """A class for defining and managing external components required by a project.

    If provided arch is a string to embed in any generated directories
    to allow compilation from from multiple systems in one source tree
    """

    def __init__(self, env, opts):
        self.__defined = {}
        self.__ = {}
        self.__required = {}
        self.__errors = {}
        self.__env = env
        self.__dry_run = GetOption('no_exec')
        self.__require_optional = GetOption('require_optional')
        self._has_icx = False
        self.download_deps = False
        self.fetch_only = False
        self.build_deps = False
        self.__parse_build_deps()
        self._replace_env(LIBTOOLIZE='libtoolize')
        self.__check_only = GetOption('check_only')
        if self.__check_only:
            # This is mostly a no_exec request.
            SetOption('no_exec', True)

        config_file = GetOption('build_config')
        if not os.path.exists(config_file):
            print(f'Config file "{config_file}" missing, cannot continue')
            Exit(1)

        self._configs = configparser.ConfigParser()
        self._configs.read(config_file)

        self.__top_dir = Dir('#').abspath
        install_dir = os.path.join(self.__top_dir, 'install')
        internal_prefix = os.path.join(self.__top_dir, 'external')

        self.deps_as_gitmodules_subdir = GetOption("deps_as_gitmodules_subdir")

        RUNNER.initialize(self.__env)

        opts.Add(PathVariable('PREFIX', 'Installation path', install_dir,
                              PathVariable.PathAccept))
        opts.Add(PathVariable('INTERNAL_PREFIX', 'Prefix for internal dependencies to be installed',
                              internal_prefix, PathVariable.PathAccept))
        opts.Add('ALT_PREFIX', f'Specifies {os.pathsep} separated list of alternative paths to add',
                 None)
        opts.Add(PathVariable('BUILD_ROOT', 'Alternative build root directory', "build",
                              PathVariable.PathIsDirCreate))
        opts.Add('USE_INSTALLED', 'Comma separated list of preinstalled dependencies', 'none')
        opts.Add(('MPI_PKG', 'Specifies name of pkg-config to load for MPI', None))
        opts.Add(BoolVariable('FIRMWARE_MGMT', 'Build in device firmware management.', False))
        opts.Add(EnumVariable('BUILD_TYPE', "Set the build type", 'release',
                              ['dev', 'debug', 'release'], ignorecase=1))
        opts.Add(EnumVariable('TARGET_TYPE', "Set the prerequisite type", 'default',
                              ['default', 'dev', 'debug', 'release'], ignorecase=1))
        opts.Add(EnumVariable('COMPILER', "Set the compiler family to use", 'gcc',
                              ['gcc', 'covc', 'clang', 'icc'], ignorecase=2))
        opts.Add(EnumVariable('WARNING_LEVEL', "Set default warning level", 'error',
                              ['warning', 'warn', 'error'], ignorecase=2))
        opts.Add(('SANITIZERS', 'Instrument C code with google sanitizers', None))

        opts.Update(self.__env)

        self._setup_compiler()

        bdir = self._setup_build_type()
        self.target_type = self.__env['TTYPE_REAL']
        self.__env['BUILD_DIR'] = bdir
        ensure_dir_exists(bdir, self.__dry_run)
        self._setup_path_var('BUILD_DIR')
        self.__build_info = BuildInfo()
        self.__build_info.update("BUILD_DIR", self.__env.subst("$BUILD_DIR"))

        # Build prerequisites in sub-dir based on selected build type
        build_dir_name = self.__env.subst('$BUILD_ROOT/external/$TTYPE_REAL')

        self.system_env = env.Clone()

        self.__build_dir = self.sub_path(build_dir_name)

        opts.Add(PathVariable('GOPATH', 'Location of your GOPATH for the build',
                              f'{self.__build_dir}/go', PathVariable.PathIsDirCreate))

        opts.Update(env)

        ensure_dir_exists(self.__build_dir, self.__dry_run)

        self.__prebuilt_path = {}
        self.__src_path = {}

        self._setup_path_var('PREFIX')
        self._setup_path_var('GOPATH')
        self.__build_info.update("PREFIX", self.__env.subst("$PREFIX"))
        self.prereq_prefix = self.__env.subst("$PREFIX/prereq/$TTYPE_REAL")
        if GetOption('install_sandbox'):
            self.prereq_prefix = f"{GetOption('install_sandbox')}/{self.prereq_prefix}"

        if config_file is not None:
            self._configs = configparser.ConfigParser()
            self._configs.read(config_file)
        else:
            self._configs = None

        self.installed = env.subst("$USE_INSTALLED").split(",")
        self.include = env.subst("$INCLUDE").split(" ")
        self._build_targets = []

        build_dir = self.__env['BUILD_DIR']
        main_targets = ['client', 'server']
        targets = ['test'] + main_targets
        self.__env.Alias('client', build_dir)
        self.__env.Alias('server', build_dir)
        self.__env.Alias('test', build_dir)
        self._build_targets = []
        check = any(item in BUILD_TARGETS for item in targets)
        if not check:
            self._build_targets.extend(['client', 'server', 'test'])
        else:
            if 'client' in BUILD_TARGETS:
                self._build_targets.append('client')
            if 'server' in BUILD_TARGETS:
                self._build_targets.append('server')
            if 'test' in BUILD_TARGETS:
                if not any(item in BUILD_TARGETS for item in main_targets):
                    print("test target requires client or server")
                    sys.exit(1)
                self._build_targets.append('test')
        BUILD_TARGETS.append(build_dir)

        env.AddMethod(self.require, 'require')

    def run_build(self, opts):
        """Build and dependencies"""
        common_reqs = ['ofi', 'hwloc', 'mercury', 'boost', 'uuid', 'crypto', 'protobufc',
                       'lz4', 'isal', 'isal_crypto', 'argobots']
        client_reqs = ['fused', 'json-c', 'capstone', 'aio']
        server_reqs = ['pmdk', 'spdk', 'ipmctl']
        test_reqs = ['cmocka']

        reqs = []
        reqs = common_reqs
        if self.test_requested():
            reqs.extend(test_reqs)
        if self.server_requested():
            reqs.extend(server_reqs)
        if self.client_requested():
            reqs.extend(client_reqs)
        opts.Add(ListVariable('DEPS', "Dependencies to build by default", 'all', reqs))
        opts.Update(self.__env)
        if GetOption('build_deps') == 'only':
            # Optionally, limit the deps we build in this pass
            reqs = self.__env.get('DEPS')

        try:
            # pylint: disable-next=import-outside-toplevel
            from components import define_components
            define_components(self)
        except Exception as old:
            raise BadScript("components", traceback.format_exc()) from old

        # Go ahead and prebuild some components
        for comp in reqs:
            if self.fetch_only:
                self.download(comp)
            else:
                self.__env.Clone().require(comp)

        if self.fetch_only:
            print("--build-deps=fetch was set, so exiting...")
            sys.exit(0)

    def _setup_build_type(self):
        """Set build type"""
        ttype = self.__env["TARGET_TYPE"]
        if ttype == "default":
            ttype = self.__env["BUILD_TYPE"]
        self.__env["TTYPE_REAL"] = ttype

        return self.__env.subst("$BUILD_ROOT/$BUILD_TYPE/$COMPILER")

    def _setup_intelc(self):
        """Setup environment to use Intel compilers"""
        try:
            env = self.__env.Clone(tools=['doneapi'])
            self._has_icx = True
        except InternalError:
            print("No oneapi compiler, trying legacy")
            env = self.__env.Clone(tools=['intelc'])
        self.__env["ENV"]["PATH"] = env["ENV"]["PATH"]
        self.__env["ENV"]["LD_LIBRARY_PATH"] = env["ENV"]["LD_LIBRARY_PATH"]
        self.__env.Replace(AR=env.get("AR"))
        self.__env.Replace(ENV=env.get("ENV"))
        self.__env.Replace(CC=env.get("CC"))
        self.__env.Replace(CXX=env.get("CXX"))
        version = env.get("INTEL_C_COMPILER_VERSION")
        self.__env.Replace(INTEL_C_COMPILER_VERSION=version)
        self.__env.Replace(LINK=env.get("LINK"))
        # disable the warning about Cilk since we don't use it
        if not self._has_icx:
            self.__env.AppendUnique(LINKFLAGS=["-static-intel",
                                               "-diag-disable=10237"])
            self.__env.AppendUnique(CCFLAGS=["-diag-disable:2282",
                                             "-diag-disable:188",
                                             "-diag-disable:2405",
                                             "-diag-disable:1338"])
        return {'CC': env.get("CC"), "CXX": env.get("CXX")}

    def _setup_compiler(self):
        """Setup the compiler to use"""
        compiler_map = {'gcc': {'CC': 'gcc', 'CXX': 'g++'},
                        'covc': {'CC': '/opt/BullseyeCoverage/bin/gcc',
                                 'CXX': '/opt/BullseyeCoverage/bin/g++',
                                 'CVS': '/opt/BullseyeCoverage/bin/covselect',
                                 'COV01': '/opt/BullseyeCoverage/bin/cov01'},
                        'clang': {'CC': 'clang', 'CXX': 'clang++'}}

        if GetOption('clean') or GetOption('help'):
            return

        compiler = self.__env.get('COMPILER')
        if compiler == 'icc':
            compiler_map['icc'] = self._setup_intelc()

        if self.__env.get('WARNING_LEVEL') == 'error':
            if compiler == 'icc' and not self._has_icx:
                warning_flag = '-Werror-all'
            else:
                warning_flag = '-Werror'
            self.__env.AppendUnique(CCFLAGS=warning_flag)

        env = self.__env.Clone()
        config = env.Configure()

        if self.__check_only:
            # Have to temporarily turn off dry run to allow this check.
            env.SetOption('no_exec', False)

        for name, prog in compiler_map[compiler].items():
            if not config.CheckProg(prog):
                print(f'{prog} must be installed when COMPILER={compiler}')
                if self.__check_only:
                    continue
                config.Finish()
                raise MissingSystemLibs(compiler, prog)
            args = {name: prog}
            self.__env.Replace(**args)

        if compiler == 'covc':
            covfile = os.path.join(self.__top_dir, 'test.cov')
            if os.path.isfile(covfile):
                os.remove(covfile)
            commands = [['$COV01', '-1'],
                        ['$COV01', '-s'],
                        ['$CVS', '--add', '!**/src/cart/test/utest/'],
                        ['$CVS', '--add', '!**/src/common/tests/'],
                        ['$CVS', '--add', '!**/src/gurt/tests/'],
                        ['$CVS', '--add', '!**/src/iosrv/tests/'],
                        ['$CVS', '--add', '!**/src/mgmt/tests/'],
                        ['$CVS', '--add', '!**/src/object/tests/'],
                        ['$CVS', '--add', '!**/src/placement/tests/'],
                        ['$CVS', '--add', '!**/src/rdb/tests/'],
                        ['$CVS', '--add', '!**/src/security/tests/'],
                        ['$CVS', '--add', '!**/src/utils/self_test/'],
                        ['$CVS', '--add', '!**/src/utils/ctl/'],
                        ['$CVS', '--add', '!**/src/vea/tests/'],
                        ['$CVS', '--add', '!**/src/vos/tests/'],
                        ['$CVS', '--add', '!**/src/engine/tests/'],
                        ['$CVS', '--add', '!**/src/tests/'],
                        ['$CVS', '--add', '!**/src/bio/smd/tests/'],
                        ['$CVS', '--add', '!**/src/cart/crt_self_test.h'],
                        ['$CVS', '--add', '!**/src/cart/crt_self_test_client.c'],
                        ['$CVS', '--add', '!**/src/cart/crt_self_test_service.c'],
                        ['$CVS', '--add', '!**/src/client/api/tests/'],
                        ['$CVS', '--add', '!**/src/client/dfuse/test/'],
                        ['$CVS', '--add', '!**/src/gurt/examples/'],
                        ['$CVS', '--add', '!**/src/utils/crt_launch/'],
                        ['$CVS', '--add', '!**/src/utils/daos_autotest.c'],
                        ['$CVS', '--add', '!**/src/placement/ring_map.c'],
                        ['$CVS', '--add', '!**/src/common/tests_dmg_helpers.c'],
                        ['$CVS', '--add', '!**/src/common/tests_lib.c']]
            if not RUNNER.run_commands(commands):
                raise BuildFailure("cov01")

        config.Finish()
        if self.__check_only:
            # Restore the dry run state
            env.SetOption('no_exec', True)

    def save_build_info(self):
        """Save build info to file for later use"""
        self.__build_info.gen_script('.build_vars.sh')
        self.__build_info.save('.build_vars.json')

    def __parse_build_deps(self):
        """Parse the build dependencies command line flag"""
        build_deps = GetOption('build_deps')
        skip_download = GetOption('skip_download')
        if build_deps in ('fetch',):
            self.fetch_only = True
        elif build_deps in ('yes', 'only'):
            self.build_deps = True
            if not skip_download:
                self.download_deps = True

    def sub_path(self, path):
        """Resolve the real path"""
        return os.path.realpath(os.path.join(self.__top_dir, path))

    def _setup_path_var(self, var):
        """Create a command line variable for a path"""
        tmp = self.__env.get(var)
        if tmp:
            value = self.sub_path(tmp)
            self.__env[var] = value

    def define(self, name, **kw):
        """Define an external prerequisite component

        Args:
            name -- The name of the component definition

        Keyword arguments:
            libs -- A list of libraries to add to dependent components
            libs_cc -- Optional CC command to test libs with.
            functions -- A list of expected functions
            headers -- A list of expected headers
            pkgconfig -- name of pkgconfig to load for installation check
            requires -- A list of names of required component definitions
            required_libs -- A list of system libraries to be checked for
            defines -- Defines needed to use the component
            package -- Name of package to install
            commands -- A list of commands to run to build the component
            config_cb -- Custom config callback
            retriever -- A retriever object to download component
            extra_lib_path -- Subdirectories to add to dependent component path
            extra_include_path -- Subdirectories to add to dependent component path
            out_of_src_build -- Build from a different directory if set to True
            build_env -- Environment variables to set for build
            skip_arch -- not required on this architecture
            static_libs -- Static libraries only, no published install
        """
        use_installed = False
        if not kw.get('static_libs', False):
            if 'all' in self.installed or name in self.installed:
                use_installed = True
        comp = _Component(self, name, use_installed, **kw)
        self.__defined[name] = comp

    def server_requested(self):
        """Return True if server build is requested"""
        return "server" in self._build_targets

    def client_requested(self):
        """Return True if client build is requested"""
        return "client" in self._build_targets

    def test_requested(self):
        """Return True if test build is requested"""
        return "test" in self._build_targets

    def _modify_prefix(self, comp_def):
        """Overwrite the prefix in cases where we may be using the default"""
        if comp_def.package:
            return

        if comp_def.src_path and \
           not os.path.exists(comp_def.src_path) and \
           not os.path.exists(os.path.join(self.prereq_prefix, comp_def.name)) and \
           not os.path.exists(self.__env.get(f'{comp_def.name.upper()}_PREFIX')):
            self._save_component_prefix(f'{comp_def.name.upper()}_PREFIX', '/usr')

    def download(self, *comps):
        """Ensure all components are downloaded"""

        for comp in comps:
            if comp not in self.__defined:
                raise MissingDefinition(comp)
            if comp in self.__errors:
                raise self.__errors[comp]
            comp_def = self.__defined[comp]
            comp_def.configure()
            comp_def.get()

    def require(self, env, *comps, **kw):
        """Ensure a component is built.

        If necessary, and add necessary libraries and paths to the construction environment.

        Args:
            env -- The construction environment to modify
            comps -- component names to configure
            kw -- Keyword arguments

        Keyword arguments:
            headers_only -- if set to True, skip library configuration
            [comp]_libs --  Override the default libs for a package.  Ignored
                            if headers_only is set
        """
        changes = False
        headers_only = kw.get('headers_only', False)
        for comp in comps:
            if comp not in self.__defined:
                raise MissingDefinition(comp)
            if comp in self.__errors:
                raise self.__errors[comp]
            comp_def = self.__defined[comp]
            if headers_only:
                needed_libs = None
            else:
                needed_libs = kw.get(f'{comp}_libs', comp_def.libs)
            if comp in self.__required:
                if GetOption('help'):
                    continue
                # checkout and build done previously
                comp_def.set_environment(env, needed_libs)
                if GetOption('clean'):
                    continue
                if self.__required[comp]:
                    changes = True
                continue
            self.__required[comp] = False
            if comp_def.is_installed(needed_libs):
                continue
            try:
                comp_def.configure()
                if comp_def.build(env, needed_libs):
                    self.__required[comp] = False
                    changes = True
                else:
                    self._modify_prefix(comp_def)
                # If we get here, just set the environment again, new directories may be present
                comp_def.set_environment(env, needed_libs)
            except Exception as error:
                # Save the exception in case the component is requested again
                self.__errors[comp] = error
                raise error

        return changes

    def included(self, *comps):
        """Returns true if the components are included in the build"""
        for comp in comps:
            if not set([comp, 'all']).intersection(set(self.include)):
                return False
        return True

    def check_component(self, *comps, **kw):
        """Returns True if a component is available"""
        env = self.__env.Clone()
        try:
            self.require(env, *comps, **kw)
        except Exception as error:  # pylint: disable=broad-except
            if self.__require_optional:
                raise error
            return False
        return True

    def is_installed(self, name):
        """Returns True if a component is available"""
        if self.check_component(name):
            return self.__defined[name].use_installed
        return False

    def get_env(self, var):
        """Get a variable from the construction environment"""
        return self.__env[var]

    def _replace_env(self, **kw):
        """Replace a values in the construction environment

        Keyword arguments:
            kw -- Arbitrary variables to replace
        """
        self.__env.Replace(**kw)

    def get_build_dir(self):
        """Get the build directory for external components"""
        return self.__build_dir

    def get_prebuilt_path(self, comp, name):
        """Get the path for a prebuilt component"""
        if name in self.__prebuilt_path:
            return self.__prebuilt_path[name]

        prebuilt_paths = self.__env.get("ALT_PREFIX")
        if prebuilt_paths is None:
            paths = []
        else:
            paths = prebuilt_paths.split(os.pathsep)

        for path in paths:
            ipath = os.path.join(path, "include")
            if not os.path.exists(ipath):
                ipath = None
            lpath = None
            for lib in comp.lib_path:
                lpath = os.path.join(path, lib)
                if os.path.exists(lpath):
                    break
                lpath = None
            if ipath is None and lpath is None:
                continue
            env = self.__env.Clone()
            if ipath:
                env.AppendUnique(CPPPATH=[ipath])
            if lpath:
                env.AppendUnique(LIBPATH=[lpath])
            realpath = os.path.realpath(path)
            if not comp.has_missing_targets(env, realpath):
                self.__prebuilt_path[name] = realpath
                return self.__prebuilt_path[name]

        self.__prebuilt_path[name] = None

        return None

    def get_component(self, name):
        """Get a component definition"""
        return self.__defined[name]

    def _save_component_prefix(self, var, value):
        """Save the component prefix in the environment and in build info"""
        self._replace_env(**{var: value})
        self.__build_info.update(var, value)

    def get_prefixes(self, name, prebuilt_path, static_libs):
        """Get the location of the scons prefix as well as the external component prefix."""
        prefix = self.__env.get('PREFIX')
        comp_prefix = f'{name.upper()}_PREFIX'
        if static_libs:
            target_prefix = os.path.join(self.__env.get('INTERNAL_PREFIX'), name)
            self._save_component_prefix(comp_prefix, target_prefix)
            return (target_prefix, prefix)
        if prebuilt_path:
            self._save_component_prefix(comp_prefix, prebuilt_path)
            return (prebuilt_path, prefix)

        target_prefix = os.path.join(self.prereq_prefix, name)
        self._save_component_prefix(comp_prefix, target_prefix)

        return (target_prefix, prefix)

    def get_src_build_dir(self):
        """Get the location of a temporary directory for hosting intermediate build files"""
        return self.__env.get('BUILD_DIR')

    def get_src_path(self, name):
        """Get the location of the sources for an external component"""
        if name in self.__src_path:
            return self.__src_path[name]
        src_path = os.path.join(self.__build_dir, name)

        self.__src_path[name] = src_path
        return src_path

    def get_config(self, section, name):
        """Get commit/patch versions"""
        if self._configs is None:
            return None
        if not self._configs.has_section(section):
            return None
        if not self._configs.has_option(section, name):
            return None
        return self._configs.get(section, name)


class _Component():
    """A class to define attributes of an external component

    Args:
        prereqs -- A PreReqComponent object
        name -- The name of the component definition
        use_installed -- check if the component is installed

    Keyword arguments:
        libs -- A list of libraries to add to dependent components
        libs_cc -- Optional compiler for testing libs
        functions -- A list of expected functions
        headers -- A list of expected headers
        requires -- A list of names of required component definitions
        commands -- A list of commands to run to build the component
        package -- Name of package to install
        config_cb -- Custom config callback
        retriever -- A retriever object to download component
        extra_lib_path -- Subdirectories to add to dependent component path
        extra_include_path -- Subdirectories to add to dependent component path
        out_of_src_build -- Build from a different directory if set to True
        patch_rpath -- Add appropriate relative rpaths to binaries
        build_env -- Environment variable(s) to add to build environment
        skip_arch -- not required on this platform
        static_libs -- Static libraries only, no public install
    """

    def __init__(self,
                 prereqs,
                 name,
                 use_installed,
                 **kw):

        self.__check_only = GetOption('check_only')
        self.__dry_run = GetOption('no_exec')
        self.targets_found = False
        self.use_installed = use_installed
        self.build_path = None
        self.prebuilt_path = None
        self.key_words = deepcopy(kw)
        self.src_path = None
        self.prefix = None
        self.component_prefix = None
        self.package = kw.get("package", None)
        self.libs = kw.get("libs", [])
        self.libs_cc = kw.get("libs_cc", None)
        self.functions = kw.get("functions", {})
        self.required_libs = kw.get("required_libs", [])
        self.required_progs = kw.get("required_progs", [])
        if kw.get("patch_rpath", []):
            self.required_progs.append("patchelf")
        self.defines = kw.get("defines", [])
        self.headers = kw.get("headers", [])
        self.requires = kw.get("requires", [])
        self.prereqs = prereqs
        self.pkgconfig = kw.get("pkgconfig", None)
        self.name = name
        self.build_commands = kw.get("commands", [])
        self.retriever = kw.get("retriever", None)
        self.lib_path = ['lib', 'lib64']
        self.include_path = ['include']
        self.lib_path.extend(default_libpath())
        self.lib_path.extend(kw.get("extra_lib_path", []))
        self.include_path.extend(kw.get("extra_include_path", []))
        self.out_of_src_build = kw.get("out_of_src_build", False)
        self.patch_path = self.prereqs.get_build_dir()
        self.skip_arch = kw.get("skip_arch", False)
        self.static_libs = kw.get("static_libs", False)

    @staticmethod
    def _sanitize_patch_path(path):
        """Remove / and https:// from path"""
        return "".join(path.split("://")[-1].split("/")[1:])

    def _resolve_patches(self):
        """Parse the patches variable"""
        patchnum = 1
        patchstr = self.prereqs.get_config("patch_versions", self.name)
        if patchstr is None:
            return {}
        patches = {}
        patch_strs = patchstr.split(",")
        for raw in patch_strs:
            patch_subdir = None
            if "^" in raw:
                (patch_subdir, raw) = raw.split('^')
            patch_name = f'{self.name}_{self._sanitize_patch_path(raw)}_{patchnum:d}'
            patch_path = os.path.join(self.patch_path, patch_name)
            patchnum += 1
            patches[patch_path] = patch_subdir
            if os.path.exists(patch_path):
                continue
            if "https://" not in raw:
                raw = os.path.join(Dir('#').abspath, raw)
                shutil.copy(raw, patch_path)
                patches[patch_path] = patch_subdir
                continue
            command = [['curl', '-sSfL', '--retry', '10', '--retry-max-time', '60',
                        '-o', patch_path, raw]]
            if not RUNNER.run_commands(command):
                raise BuildFailure(raw)
        # Remove old patches
        for fname in os.listdir(self.patch_path):
            if not fname.startswith(f"{self.name}_"):
                continue
            found = False
            for key in patches:
                if fname in key:
                    found = True
                    break
            if not found:
                old_patch = os.path.join(self.patch_path, fname)
                print(f"Removing old, unused patch file {old_patch}")
                os.unlink(old_patch)

        return patches

    def get(self):
        """Download the component sources, if necessary"""
        if self.prebuilt_path:
            print(f'Using prebuilt binaries for {self.name}')
            return
        branch = self.prereqs.get_config("branches", self.name)
        commit_sha = self.prereqs.get_config("commit_versions", self.name)
        repo = self.prereqs.get_config("repos", self.name)

        if self.prereqs.deps_as_gitmodules_subdir is None and \
                not self.retriever:
            print(f"Using installed version of {self.name}")
            return

        if self.prereqs.deps_as_gitmodules_subdir:
            target = os.path.join(
                self.prereqs.sub_path(self.prereqs.deps_as_gitmodules_subdir),
                self.name)

            if not os.path.isdir(target):
                print(f"Symlink target {target} is not a valid directory")
                raise BuildFailure(self.name)

            self.retriever = CopyRetriever(source=target, patched=True)

        # Source code is retrieved using retriever
        if not (self.prereqs.download_deps or self.prereqs.fetch_only):
            if self.prereqs.build_deps:
                print("Assuming sources have been downloaded already")
                return

        patches = self._resolve_patches()
        self.retriever.get(self.name, self.src_path, repo, commit_sha=commit_sha,
                           patches=patches, branch=branch)

    def _has_missing_system_deps(self, env):
        """Check for required system libs"""
        if self.__check_only:
            # Have to temporarily turn off dry run to allow this check.
            env.SetOption('no_exec', False)

        if env.GetOption('no_exec'):
            # Can not override no-check in the command line.
            print('Would check for missing system libraries')
            return False

        if GetOption('help'):
            return True

        config = env.Configure()

        for lib in self.required_libs:
            if not config.CheckLib(lib):
                config.Finish()
                if self.__check_only:
                    env.SetOption('no_exec', True)
                return True

        for prog in self.required_progs:
            if not config.CheckProg(prog):
                config.Finish()
                if self.__check_only:
                    env.SetOption('no_exec', True)
                return True

        config.Finish()
        if self.__check_only:
            env.SetOption('no_exec', True)
        return False

    def _parse_config(self, env, opts, comp_path=None):
        """Parse a pkg-config file"""
        if self.pkgconfig is None:
            return

        real_comp_path = self.component_prefix
        if comp_path:
            real_comp_path = comp_path

        path = os.environ.get("PKG_CONFIG_PATH", None)
        if path and "PKG_CONFIG_PATH" not in env["ENV"]:
            env["ENV"]["PKG_CONFIG_PATH"] = path
        if (not self.use_installed and real_comp_path is not None
           and not real_comp_path == "/usr"):
            path_found = False
            for path in self.lib_path:
                config = os.path.join(real_comp_path, path, "pkgconfig")
                if not os.path.exists(config):
                    continue
                path_found = True
                env.AppendENVPath("PKG_CONFIG_PATH", config)
            if not path_found:
                return

        try:
            env.ParseConfig(f'pkg-config {opts} {self.pkgconfig}')
        except OSError:
            return

        return

    def _print(self, msg):
        if GetOption('silent'):
            return
        print(msg)

    def has_missing_targets(self, env, comp_path=None):
        """Check for expected build targets (e.g. libraries or headers)"""
        # pylint: disable=too-many-return-statements
        if self.targets_found:
            return False

        if self.skip_arch:
            self.targets_found = True
            return False

        if self.__check_only:
            # Temporarily turn off dry-run.
            env.SetOption('no_exec', False)

        if env.GetOption('no_exec'):
            # Can not turn off dry run set by command line.
            print("Would check for missing build targets")
            return True

        # No need to fail here if we can't find the config, it may not always be generated
        self._parse_config(env, "--cflags --libs-only-L", comp_path=comp_path)

        for define in self.defines:
            env.AppendUnique(CPPDEFINES=[define])

        if GetOption('help'):
            print('help set')
            return True

        self._print(f"Checking targets for component '{self.name}'")

        config = env.Configure()
        config_cb = self.key_words.get("config_cb", None)
        if config_cb:
            if not config_cb(config):
                config.Finish()
                if self.__check_only:
                    env.SetOption('no_exec', True)
                print('Custom check failed')
                return True

        for prog in self.key_words.get("progs", []):
            if not config.CheckProg(prog):
                config.Finish()
                if self.__check_only:
                    env.SetOption('no_exec', True)
                return True

        for header in self.headers:
            if not config.CheckHeader(header):
                config.Finish()
                if self.__check_only:
                    env.SetOption('no_exec', True)
                return True

        for lib in self.libs:
            old_cc = env.subst('$CC')
            if self.libs_cc:
                arg_key = f'{self.name.upper()}_PREFIX'
                args = {arg_key: self.component_prefix}
                env.Replace(**args)
                env.Replace(CC=self.libs_cc)
            result = config.CheckLib(lib)
            env.Replace(CC=old_cc)
            if not result:
                config.Finish()
                if self.__check_only:
                    env.SetOption('no_exec', True)
                return True

        for lib, functions in self.functions.items():
            saved_env = config.env.Clone()
            config.env.AppendUnique(LIBS=[lib])
            for function in functions:
                result = config.CheckFunc(function)
                if not result:
                    config.Finish()
                    if self.__check_only:
                        env.SetOption('no_exec', True)
                    return True
            config.env = saved_env

        config.Finish()
        self.targets_found = True
        if self.__check_only:
            env.SetOption('no_exec', True)
        return False

    def is_installed(self, needed_libs):
        """Check if the component is already installed"""
        if not self.use_installed:
            return False
        new_env = self.prereqs.system_env.Clone()
        self.set_environment(new_env, needed_libs)
        if self.has_missing_targets(new_env):
            self.use_installed = False
            print(f"{self.name} failed install check")
            return False
        print(f"{self.name} passed install check")
        return True

    def configure(self):
        """Setup paths for a required component"""
        if self.skip_arch:
            return

        if not self.retriever:
            self.prebuilt_path = "/usr"
        else:
            self.prebuilt_path = self.prereqs.get_prebuilt_path(self, self.name)

        (self.component_prefix, self.prefix) = self.prereqs.get_prefixes(self.name,
                                                                         self.prebuilt_path,
                                                                         self.static_libs)
        self.src_path = None
        if self.retriever:
            self.src_path = self.prereqs.get_src_path(self.name)
        self.build_path = self.src_path
        if self.out_of_src_build:
            self.build_path = os.path.join(self.prereqs.get_build_dir(), f'{self.name}.build')

            ensure_dir_exists(self.build_path, self.__dry_run)

    def set_environment(self, env, needed_libs):
        """Modify the specified construction environment to build with the external component"""
        if self.skip_arch:
            return

        lib_paths = []

        # Make sure CheckProg() looks in the component's bin/ dir
        if not self.use_installed and not self.component_prefix == "/usr":
            env.AppendENVPath('PATH', os.path.join(self.component_prefix, 'bin'))

            for path in self.include_path:
                env.AppendUnique(CPPPATH=[os.path.join(self.component_prefix, path)])

            # The same rules that apply to headers apply to RPATH.   If a build
            # uses a component, that build needs the RPATH of the dependencies.
            for path in self.lib_path:
                full_path = os.path.join(self.component_prefix, path)
                if not os.path.exists(full_path):
                    continue
                lib_paths.append(full_path)
                # will adjust this to be a relative rpath later
                env.AppendUnique(RPATH_FULL=[full_path])

            # Ensure RUNPATH is used rather than RPATH.  RPATH is deprecated
            # and this allows LD_LIBRARY_PATH to override RPATH
            env.AppendUnique(LINKFLAGS=["-Wl,--enable-new-dtags"])
        if self.component_prefix == "/usr" and self.package is None:
            # hack until we have everything installed in lib64
            env.AppendUnique(RPATH=["/usr/lib"])
            env.AppendUnique(LINKFLAGS=["-Wl,--enable-new-dtags"])

        for define in self.defines:
            env.AppendUnique(CPPDEFINES=[define])

        self._parse_config(env, "--cflags")

        if needed_libs is None:
            return

        self._parse_config(env, "--libs")
        for path in lib_paths:
            env.AppendUnique(LIBPATH=[path])
        for lib in needed_libs:
            env.AppendUnique(LIBS=[lib])

    def _set_build_env(self, env):
        """Add any environment variables to build environment"""
        env["ENV"].update(self.key_words.get("build_env", {}))

    def _check_installed_package(self, env):
        """Check installed targets"""
        if self.has_missing_targets(env):
            if self.__dry_run:
                if self.package is None:
                    print(f'Missing {self.name}')
                else:
                    print(f'Missing package {self.package} {self.name}')
                return
            if self.package is None:
                raise MissingTargets(self.name, self.name)

            raise MissingTargets(self.name, self.package)

    def _check_user_options(self, env, needed_libs):
        """Check help and clean options"""
        if GetOption('help'):
            if self.requires:
                self.prereqs.require(env, *self.requires)
            return True

        self.set_environment(env, needed_libs)
        if GetOption('clean'):
            return True
        return False

    def _rm_old_dir(self, path):
        """Remove the old dir"""
        if self.__dry_run:
            print(f'Would empty {path}')
        else:
            shutil.rmtree(path)
            os.mkdir(path)

    def _patch_rpaths(self):
        """Run patchelf binary to add relative rpaths"""
        rpath = ["$$ORIGIN"]
        norigin = []
        comp_path = self.component_prefix
        if not comp_path:
            return
        if comp_path.startswith('/usr') and '/prereq/' not in comp_path:
            return
        if not os.path.exists(comp_path):
            return

        for libdir in self.lib_path:
            path = os.path.join(comp_path, libdir)
            if os.path.exists(path):
                norigin.append(os.path.normpath(path))
                break

        for prereq in self.requires:
            rootpath = os.path.join(comp_path, '..', prereq)
            if not os.path.exists(rootpath):
                comp = self.prereqs.get_component(prereq)
                subpath = comp.component_prefix
                if subpath and not subpath.startswith("/usr"):
                    for libdir in self.lib_path:
                        lpath = os.path.join(subpath, libdir)
                        if not os.path.exists(lpath):
                            continue
                        rpath.append(lpath)
                continue

            for libdir in self.lib_path:
                path = os.path.join(rootpath, libdir)
                if not os.path.exists(path):
                    continue
                rpath.append(f'$$ORIGIN/../../{prereq}/{libdir}')
                norigin.append(os.path.normpath(path))
                break

        rpath += norigin
        for folder in self.key_words.get("patch_rpath", []):
            path = os.path.join(comp_path, folder)
            files = os.listdir(path)
            for lib in files:
                if folder != 'bin' and not lib.endswith(".so"):
                    # Assume every file in bin can be patched
                    continue
                if lib.endswith(".py"):
                    continue
                full_lib = os.path.join(path, lib)
                cmd = ['patchelf', '--set-rpath', ':'.join(rpath), full_lib]
                res = RUNNER.run_commands([cmd])
                if not res:
                    if lib in ('libspdk.so', 'spdk_cli', 'spdk_rpc'):
                        print(f'Skipped patching {full_lib}')
                    else:
                        raise BuildFailure(f'Error running patchelf on {full_lib}')

    def build(self, env, needed_libs):
        """Build the component, if necessary

        :param env: Scons Environment for building.
        :type env: environment
        :param needed_libs: Only configure libs in list
        :return: True if the component needed building.
        """
        # Ensure requirements are met
        changes = False
        envcopy = self.prereqs.system_env.Clone()

        if self._check_user_options(env, needed_libs):
            return True
        self.set_environment(envcopy, self.libs)
        if self.prebuilt_path:
            self._check_installed_package(envcopy)
            return False

        build_dep = self.prereqs.build_deps
        if self.use_installed:
            print(f"{self.name} should be installed")
            build_dep = False
        if self.component_prefix and os.path.exists(self.component_prefix):
            print(f"{self.name} already has a build directory")
            build_dep = False

        # If a component has both a package name and builder then check if the package can be used
        # before building.  This allows a rpm to be built if available but source to be used
        # if not.
        if build_dep:
            if self.package:
                missing_targets = self.has_missing_targets(envcopy)
                if not missing_targets:
                    print(f"{self.name} is not actually installed, building...")
                    build_dep = False
        else:
            missing_targets = self.has_missing_targets(envcopy)

        if build_dep:

            if self._has_missing_system_deps(self.prereqs.system_env):
                raise MissingSystemLibs(self.name, self.required_progs)

            self.get()

            if self.requires:
                changes = self.prereqs.require(envcopy, *self.requires, needed_libs=None)
                self.set_environment(envcopy, self.libs)

            ensure_dir_exists(self.prereqs.prereq_prefix, self.__dry_run)
            changes = True
            if self.out_of_src_build:
                self._rm_old_dir(self.build_path)
            self._set_build_env(envcopy)
            if not RUNNER.run_commands(self.build_commands, subdir=self.build_path, env=envcopy):
                raise BuildFailure(self.name)

        elif missing_targets:
            if self.__dry_run:
                print(f'Would do required build of {self.name}')
            else:
                raise BuildRequired(self.name)

        # set environment one more time as new directories may be present
        if self.requires:
            self.prereqs.require(envcopy, *self.requires, needed_libs=None)
        self.set_environment(envcopy, self.libs)
        if changes:
            self._patch_rpaths()
        if self.has_missing_targets(envcopy) and not self.__dry_run:
            raise MissingTargets(self.name, None)
        return changes


__all__ = ["GitRepoRetriever", "CopyRetriever", "DownloadFailure", "BadScript", "BuildFailure",
           "MissingDefinition", "MissingTargets", "MissingSystemLibs", "DownloadRequired",
           "PreReqComponent", "BuildRequired"]

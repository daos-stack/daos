#!/usr/bin/python
# Copyright 2016-2022 Intel Corporation
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

# pylint: disable=too-few-public-methods
# pylint: disable=too-many-instance-attributes
# pylint: disable=broad-except
# pylint: disable=bare-except
# pylint: disable=exec-used
# pylint: disable=too-many-statements
# pylint: disable=too-many-lines
from __future__ import absolute_import, division, print_function
import os
import traceback
import hashlib
import time
import sys
import errno
import shutil
from build_info import BuildInfo
from SCons.Variables import PathVariable
from SCons.Variables import EnumVariable
from SCons.Variables import ListVariable
from SCons.Variables import BoolVariable
from SCons.Script import Dir
from SCons.Script import GetOption
from SCons.Script import SetOption
from SCons.Script import Configure
from SCons.Script import AddOption
from SCons.Script import WhereIs
from SCons.Script import SConscript
from SCons.Script import BUILD_TARGETS
from SCons.Errors import InternalError
# pylint: disable=no-name-in-module
# pylint: disable=import-error
from SCons.Errors import UserError
# pylint: enable=no-name-in-module
# pylint: enable=import-error
from prereq_tools import mocked_tests
import subprocess  # nosec
try:
    from subprocess import DEVNULL  # nosec
except ImportError:
    DEVNULL = open(os.devnull, "wb")
import tarfile
import copy
if sys.version_info < (3, 0):
    # pylint: disable=import-error
    import ConfigParser
else:
    import configparser as ConfigParser

OPTIONAL_COMPS = ['psm2']


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
        """ Exception string """
        return "Failed to get %s from %s" % (self.component, self.repo)


class ExtractionError(Exception):
    """Exception raised when source couldn't be extracted

    Attributes:
        component -- Component
        reason    -- Reason for problem
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """ Exception string """

        return "Failed to extract %s" % self.component


class UnsupportedCompression(Exception):
    """Exception raised when library doesn't support extraction method

    Attributes:
        component -- Component
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "Don't know how to extract %s" % self.component


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
        """ Exception string """
        return "Failed to execute %s:\n%s %s\n\nTraceback" % (self.script,
                                                              self.script,
                                                              self.trace)


class MissingDefinition(Exception):
    """Exception raised when component has no definition

    Attributes:
        component    -- component that is missing definition
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "No definition for %s" % self.component


class MissingPath(Exception):
    """Exception raised when user specifies a path that doesn't exist

    Attributes:
        variable    -- Variable specified
    """

    def __init__(self, variable):
        super().__init__()
        self.variable = variable

    def __str__(self):
        """ Exception string """
        return "%s specifies a path that doesn't exist" % self.variable


class BuildFailure(Exception):
    """Exception raised when component fails to build

    Attributes:
        component    -- component that failed to build
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s failed to build" % self.component


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
        """ Exception string """
        if self.package is None:
            return "%s has missing targets after build.  " \
                   "See config.log for details" % self.component
        return "Package %s is required. " \
               "Check config.log" % self.package


class MissingSystemLibs(Exception):
    """Exception raised when libraries required for target build are not met

    Attributes:
        component    -- component that has missing targets
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s has unmet dependencies required for build" % self.component


class DownloadRequired(Exception):
    """Exception raised when component is missing but downloads aren't enabled

    Attributes:
        component    -- component that is missing
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s needs to be built, use --build-deps=yes" % self.component


class BuildRequired(Exception):
    """Exception raised when component is missing but builds aren't enabled

    Attributes:
        component    -- component that is missing
    """

    def __init__(self, component):
        super().__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s needs to be built, use --build-deps=yes" % self.component


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
        """Runs a set of commands in specified directory"""
        if not self.env:
            raise Exception("PreReqComponent not initialized")
        retval = True

        passed_env = env or self.env

        if subdir:
            print('Running commands in {}'.format(subdir))
        for command in commands:
            cmd = []
            for part in command:
                if part == 'make':
                    cmd.extend(['make', '-j', str(GetOption('num_jobs'))])
                else:
                    cmd.append(self.env.subst(part))
            if self.__dry_run:
                print('Would RUN: %s' % ' '.join(cmd))
                retval = True
            else:
                print('RUN: %s' % ' '.join(cmd))
                if subprocess.call(cmd, shell=False, cwd=subdir, env=passed_env['ENV']) != 0:
                    retval = False
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
        pipe = subprocess.Popen([dpkgarchitecture, '-qDEB_HOST_MULTIARCH'],
                                stdout=subprocess.PIPE, stderr=DEVNULL)
        (stdo, _) = pipe.communicate()
        if pipe.returncode == 0:
            archpath = stdo.decode().strip()
            return ['lib/' + archpath]
    except Exception:
        print('default_libpath, Exception: subprocess.Popen dpkg-architecture')
    return []


class GitRepoRetriever():
    """Identify a git repository from which to download sources"""

    def __init__(self, url, has_submodules=False, branch=None):

        self.url = url
        self.has_submodules = has_submodules
        self.branch = branch
        self.commit_sha = None

    def checkout_commit(self, subdir):
        """ checkout a certain commit SHA or branch """
        if self.commit_sha is not None:
            commands = [['git', 'checkout', self.commit_sha]]
            if not RUNNER.run_commands(commands, subdir=subdir):
                raise DownloadFailure(self.url, subdir)

    def apply_patches(self, subdir, patches):
        """ git-apply a certain hash """
        if patches is not None:
            for patch in patches.keys():
                print("Applying patch %s" % (patch))
                command = ['git', 'apply']
                if patches[patch] is not None:
                    command.extend(['--directory', patches[patch]])
                command.append(patch)
                if not RUNNER.run_commands([command], subdir=subdir):
                    raise DownloadFailure(self.url, subdir)

    def update_submodules(self, subdir):
        """ update the git submodules """
        if self.has_submodules:
            commands = [['git', 'submodule', 'init'], ['git', 'submodule', 'update']]
            if not RUNNER.run_commands(commands, subdir=subdir):
                raise DownloadFailure(self.url, subdir)

    def get(self, subdir, **kw):
        """Downloads sources from a git repository into subdir"""

        # Now checkout the commit_sha if specified
        passed_commit_sha = kw.get("commit_sha", None)
        if passed_commit_sha is None:
            comp = os.path.basename(subdir)
            print("""
*********************** ERROR ************************
No commit_versions entry in utils/build.config for
%s. Please specify one to avoid breaking the
build with random upstream changes.
*********************** ERROR ************************\n""" % comp)
            raise DownloadFailure(self.url, subdir)

        if not os.path.exists(subdir):
            commands = [['git', 'clone', self.url, subdir]]
            if not RUNNER.run_commands(commands):
                raise DownloadFailure(self.url, subdir)
        self.get_specific(subdir, **kw)

    def get_specific(self, subdir, **kw):
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
        self.update_submodules(subdir)
        # Now apply any patches specified
        self.apply_patches(subdir, kw.get("patches", {}))


class WebRetriever():
    """Identify a location from where to download a source package"""

    def __init__(self, url, md5):
        self.url = url
        self.md5 = md5
        self.__dry_run = GetOption('check_only')
        if self.__dry_run:
            SetOption('no_exec', True)
        self.__dry_run = GetOption('no_exec')

    def check_md5(self, filename):
        """Return True if md5 matches"""

        if not os.path.exists(filename):
            return False

        with open(filename, "rb") as src:
            hexdigest = hashlib.md5(src.read()).hexdigest()  # nosec

        if hexdigest != self.md5:
            print("Removing existing file %s: md5 %s != %s" % (filename,
                                                               self.md5,
                                                               hexdigest))
            os.remove(filename)
            return False

        print("File %s matches md5 %s" % (filename, self.md5))
        return True

    def download(self, basename):
        """Download the file"""
        initial_sleep = 1
        retries = 3
        # Retry download a few times if it fails
        for i in range(0, retries + 1):
            command = ['curl',
                       '-sSf',
                       '--location',
                       '--remote-name',
                       self.url]

            failure_reason = "Download command failed"
            if RUNNER.run_commands(command):
                if self.check_md5(basename):
                    print("Successfully downloaded %s" % self.url)
                    return True

                failure_reason = "md5 mismatch"

            print("Try #%d to get %s failed: %s" % (i + 1, self.url, failure_reason))

            if i != retries:
                time.sleep(initial_sleep)
                initial_sleep *= 2

        return False

    def get(self, subdir, **kw):  # pylint: disable=unused-argument
        """Downloads and extracts sources from a url into subdir"""

        basename = os.path.basename(self.url)

        if os.path.exists(subdir):
            # assume that nothing has changed
            return

        if not self.check_md5(basename) and not self.download(basename):
            raise DownloadFailure(self.url, subdir)

        if self.url.endswith('.tar.gz') or self.url.endswith('.tgz'):
            if self.__dry_run:
                print('Would unpack gzipped tar file: %s' % basename)
                return
            try:
                with tarfile.open(basename, 'r:gz') as tfile:
                    members = tfile.getnames()
                    prefix = os.path.commonprefix(members)
                    tfile.extractall()
                os.rename(prefix, subdir)
            except (IOError, tarfile.TarError) as io_error:
                print(traceback.format_exc())
                raise ExtractionError(subdir) from io_error
        else:
            raise UnsupportedCompression(subdir)


def check_flag_helper(context, compiler, ext, flag):
    """Helper function to allow checking for compiler flags"""
    if compiler in ["icc", "icpc"]:
        flags = ["-diag-error=10006", "-diag-error=10148",
                 "-Werror-all", flag]
        # bug in older scons, need CFLAGS to exist, -O2 is default.
        context.env.Replace(CFLAGS=['-O2'])
    elif compiler in ["gcc", "g++"]:
        # remove -no- for test
        test_flag = flag.replace("-Wno-", "-W")
        flags = ["-Werror", test_flag]
    else:
        flags = ["-Werror", flag]
    context.Message("Checking %s %s " % (compiler, flag))
    context.env.Replace(CCFLAGS=flags)
    ret = context.TryCompile("""
int main() {
    return 0;
}
""", ext)
    context.Result(ret)
    return ret


def check_flag(context, flag):
    """Check C specific compiler flags"""
    return check_flag_helper(context, context.env.get("CC"), ".c", flag)


def check_flag_cc(context, flag):
    """Check C++ specific compiler flags"""
    return check_flag_helper(context, context.env.get("CXX"), ".cpp", flag)


def check_flags(env, config, key, value):
    """Check and append all supported flags"""
    if GetOption('help') or GetOption('clean'):
        return
    checked = []
    for flag in value:
        if flag in checked:
            continue
        insert = False
        if key == "CCFLAGS":
            if config.CheckFlag(flag) and config.CheckFlagCC(flag):
                insert = True
        elif key == "CFLAGS":
            if config.CheckFlag(flag):
                insert = True
        elif config.CheckFlagCC(flag):
            insert = True
        if insert:
            env.AppendUnique(**{key: [flag]})
        checked.append(flag)


def append_if_supported(env, **kwargs):
    """Check and append flags for construction variables"""
    cenv = env.Clone()
    config = Configure(cenv, custom_tests={'CheckFlag': check_flag,
                                           'CheckFlagCC': check_flag_cc})
    for key, value in kwargs.items():
        if key not in ["CFLAGS", "CXXFLAGS", "CCFLAGS"]:
            env.AppendUnique(**{key: value})
            continue
        check_flags(env, config, key, value)

    config.Finish()


class ProgramBinary():
    """Define possible names for a required executable"""

    def __init__(self, name, possible_names):
        """ Define a binary allowing for unique names on various platforms """
        self.name = name
        self.options = possible_names

    def configure(self, config, prereqs):
        """ Do configure checks for binary name options to get a match """
        for option in self.options:
            if config.CheckProg(option):
                args = {self.name: option}
                prereqs.replace_env(**args)
                return True
        return False


def ensure_dir_exists(dirname, dry_run):
    """Ensure a directory exists"""
    if not os.path.exists(dirname):
        if dry_run:
            print("Would create %s" % dry_run)
            return
        try:
            os.makedirs(dirname)
        except Exception as e:
            if not os.path.isdir(dirname):
                raise e

    if not os.path.isdir(dirname):
        raise IOError(errno.ENOTDIR, 'Not a directory', dirname)

# pylint: disable=too-many-public-methods


class PreReqComponent():
    """A class for defining and managing external components required
       by a project.

    If provided arch is a string to embed in any generated directories
    to allow compilation from from multiple systems in one source tree
    """

# pylint: disable=too-many-branches

    def __init__(self, env, variables, config_file=None, arch=None):
        self.__defined = {}
        self.__required = {}
        self.__errors = {}
        self.__env = env
        self.__opts = variables
        self.configs = None

        real_env = self.__env['ENV']

        for var in ["HOME", "TERM", "SSH_AUTH_SOCK",
                    "http_proxy", "https_proxy",
                    "PKG_CONFIG_PATH", "MODULEPATH",
                    "MODULESHOME", "MODULESLOADED",
                    "I_MPI_ROOT"]:
            value = os.environ.get(var)
            if value:
                real_env[var] = value

        libtoolize = 'libtoolize'
        if self.__env['PLATFORM'] == 'darwin':
            libtoolize = 'glibtoolize'

        self.__dry_run = GetOption('no_exec')
        self.add_options()
        self.__env.AddMethod(append_if_supported, "AppendIfSupported")
        self.__env.AddMethod(mocked_tests.build_mock_unit_tests,
                             'BuildMockingUnitTests')
        self.__require_optional = GetOption('require_optional')
        self.has_icx = False
        self.download_deps = False
        self.build_deps = False
        self.__parse_build_deps()
        self.replace_env(LIBTOOLIZE=libtoolize)
        self.__env.Replace(ENV=real_env)
        warning_level = self.__env.subst("$WARNING_LEVEL")
        pre_path = GetOption('prepend_path')
        if pre_path:
            old_path = self.__env['ENV']['PATH']
            self.__env['ENV']['PATH'] = pre_path + os.pathsep + old_path
        locale_name = GetOption('locale_name')
        if locale_name:
            self.__env['ENV']['LC_ALL'] = locale_name
        self.__check_only = GetOption('check_only')
        if self.__check_only:
            # This is mostly a no_exec request.
            SetOption('no_exec', True)
        if config_file is None:
            config_file = GetOption('build_config')

        RUNNER.initialize(self.__env)

        self._setup_user_prefix()

        self.__top_dir = Dir('#').abspath
        self._setup_compiler(warning_level)
        self.add_opts(PathVariable('BUILD_ROOT',
                                   'Alternative build root dierctory', "build",
                                   PathVariable.PathIsDirCreate))

        bdir = self._setup_build_type()
        self.target_type = self.__env.get("TTYPE_REAL")
        self.__env["BUILD_DIR"] = bdir
        ensure_dir_exists(bdir, self.__dry_run)
        self.setup_path_var('BUILD_DIR')
        self.__build_info = BuildInfo()
        self.__build_info.update("BUILD_DIR", self.__env.subst("$BUILD_DIR"))

        build_dir_name = os.path.join(self.__env.get("BUILD_ROOT"), "external")
        install_dir = os.path.join(self.__top_dir, 'install')
        if arch:
            build_dir_name += '-%s' % arch
            install_dir = os.path.join('install', str(arch))

            # Overwrite default file locations to allow multiple builds in the
            # same tree.
            env.SConsignFile('.sconsign-%s' % arch)
            env.Replace(CONFIGUREDIR='#/.sconf-temp-%s' % arch,
                        CONFIGURELOG='#/config-%s.log' % arch)

        # Build pre-reqs in sub-dir based on selected build type
        build_dir_name = os.path.join(build_dir_name,
                                      self.__env.subst("$TTYPE_REAL"))

        self.add_opts(PathVariable('ENV_SCRIPT',
                                   "Location of environment script",
                                   os.path.expanduser('~/.scons_localrc'),
                                   PathVariable.PathAccept))

        env_script = self.__env.get("ENV_SCRIPT")
        if os.path.exists(env_script):
            SConscript(env_script, exports=['env'])

        self.system_env = env.Clone()

        self.__build_dir = os.path.realpath(os.path.join(self.__top_dir,
                                                         build_dir_name))
        ensure_dir_exists(self.__build_dir, self.__dry_run)

        self.__prebuilt_path = {}
        self.__src_path = {}

        self.__opts.Add('USE_INSTALLED',
                        'Comma separated list of preinstalled dependencies',
                        'none')
        self.add_opts(ListVariable('INCLUDE', "Optional components to build",
                                   'none', OPTIONAL_COMPS))
        self.add_opts(('MPI_PKG',
                       'Specifies name of pkg-config to load for MPI', None))
        self.add_opts(BoolVariable('FIRMWARE_MGMT',
                                   'Build in device firmware management.', 0))
        self.add_opts(PathVariable('PREFIX', 'Installation path', install_dir,
                                   PathVariable.PathIsDirCreate),
                      PathVariable('GOPATH',
                                   'Location of your GOPATH for the build',
                                   "%s/go" % self.__build_dir,
                                   PathVariable.PathIsDirCreate))
        self.setup_path_var('PREFIX')
        self.setup_path_var('GOPATH')
        self.__build_info.update("PREFIX", self.__env.subst("$PREFIX"))
        self.prereq_prefix = self.__env.subst("$PREFIX/prereq/$TTYPE_REAL")
        self._setup_parallel_build()

        self.config_file = config_file
        if config_file is not None:
            self.configs = ConfigParser.ConfigParser()
            self.configs.read(config_file)

        self.installed = env.subst("$USE_INSTALLED").split(",")
        self.include = env.subst("$INCLUDE").split(" ")
        self._build_targets = []

    def init_build_targets(self, build_dir):
        """Setup default build targets"""
        targets = ['test', 'server', 'client']
        self.__env.Alias('client', build_dir)
        self.__env.Alias('server', build_dir)
        self.__env.Alias('test', build_dir)
        self._build_targets = []
        check = any(item in BUILD_TARGETS for item in targets)
        if not check or 'test' in BUILD_TARGETS:
            self._build_targets.extend(['client', 'server', 'test'])
        else:
            if 'client' in BUILD_TARGETS:
                self._build_targets.append('client')
            if 'server' in BUILD_TARGETS:
                self._build_targets.append('server')
        BUILD_TARGETS.append(build_dir)

    def has_source(self, env, *comps, **kw):
        """Check if source exists for a component"""
        new_env = env.Clone()
        # first require the binary of the component.
        self.require(new_env, *comps, **kw)

        for comp in comps:
            try:
                path = self.get_src_path(comp)
                if not os.path.exists(path):
                    return False
            except MissingPath:
                print("%s source not found" % comp)
                return False

        return True

# pylint: enable=too-many-branches
    def _setup_user_prefix(self):
        """setup ALT_PREFIX option"""
        self.add_opts(('ALT_PREFIX',
                       'Specifies %s separated list of alternative paths to add'
                       % os.pathsep, None))

    def _setup_build_type(self):
        """set build type"""
        self.add_opts(EnumVariable('BUILD_TYPE', "Set the build type",
                                   'release', ['dev', 'debug', 'release'],
                                   ignorecase=1))
        self.add_opts(EnumVariable('TARGET_TYPE', "Set the prerequisite type",
                                   'default',
                                   ['default', 'dev', 'debug', 'release'],
                                   ignorecase=1))
        ttype = self.__env["TARGET_TYPE"]
        if ttype == "default":
            ttype = self.__env["BUILD_TYPE"]
        self.__env["TTYPE_REAL"] = ttype

        return self.__env.subst("$BUILD_ROOT/$BUILD_TYPE/$COMPILER")

    def _setup_intelc(self):
        """Setup environment to use intel compilers"""
        try:
            env = self.__env.Clone(tools=['doneapi'])
            self.has_icx = True
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
        if not self.has_icx:
            self.__env.AppendUnique(LINKFLAGS=["-static-intel",
                                               "-diag-disable=10237"])
            self.__env.AppendUnique(CCFLAGS=["-diag-disable:2282",
                                             "-diag-disable:188",
                                             "-diag-disable:2405",
                                             "-diag-disable:1338"])
        return {'CC': env.get("CC"), "CXX": env.get("CXX")}

    def _setup_compiler(self, warning_level):
        """Setup the compiler to use"""
        compiler_map = {'gcc': {'CC': 'gcc', 'CXX': 'g++'},
                        'covc': {'CC': '/opt/BullseyeCoverage/bin/gcc',
                                 'CXX': '/opt/BullseyeCoverage/bin/g++',
                                 'CVS': '/opt/BullseyeCoverage/bin/covselect',
                                 'COV01': '/opt/BullseyeCoverage/bin/cov01'},
                        'clang': {'CC': 'clang', 'CXX': 'clang++'}}
        self.add_opts(EnumVariable('COMPILER', "Set the compiler family to use",
                                   'gcc', ['gcc', 'covc', 'clang', 'icc'],
                                   ignorecase=1))

        if GetOption('clean') or GetOption('help'):
            return

        compiler = self.__env.get('COMPILER').lower()
        if compiler == 'icc':
            compiler_map['icc'] = self._setup_intelc()

        if warning_level == 'error':
            if compiler == 'icc' and not self.has_icx:
                warning_flag = '-Werror-all'
            else:
                warning_flag = '-Werror'
            self.__env.AppendUnique(CCFLAGS=warning_flag)

        env = self.__env.Clone()
        config = Configure(env)

        if self.__check_only:
            # Have to temporarily turn off dry run to allow this check.
            env.SetOption('no_exec', False)

        for name, prog in compiler_map[compiler].items():
            if not config.CheckProg(prog):
                print("%s must be installed when COMPILER=%s" % (prog, compiler))
                if self.__check_only:
                    continue
                config.Finish()
                raise MissingSystemLibs(prog)
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

    def _setup_parallel_build(self):
        """Set the parallel options for builds"""
        # Multiple go jobs can be running at once via the -j option so limit each to 1 proc.
        # This allows for compilation to continue on systems with limited processor resources where
        # the number of go procs will be multiplied by jobs_opt.
        self.__env["ENV"]["GOMAXPROCS"] = "1"

    def get_build_info(self):
        """Retrieve the BuildInfo"""
        return self.__build_info

    def get_config_file(self):
        """Retrieve the Config File"""
        return self.config_file

    def add_options(self):
        """Add common options to environment"""

        AddOption('--require-optional',
                  dest='require_optional',
                  action='store_true',
                  default=False,
                  help='Fail the build if check_component fails')

        # There is another case here which isn't handled, we want Jenkins
        # builds to download tar packages but not git source code.  For now
        # just have a single flag and set this for the Jenkins builds which
        # need this.
        AddOption('--build-deps',
                  dest='build_deps',
                  type='choice',
                  choices=['yes', 'no', 'build-only'],
                  default='no',
                  help="Automatically download and build sources.  (yes|no|build-only) [no]")

        # We want to be able to check what dependencies are needed with out
        # doing a build, similar to --dry-run.  We can not use --dry-run
        # on the command line because it disables running the tests for the
        # the dependencies.  So we need a new option
        AddOption('--check-only',
                  dest='check_only',
                  action='store_true',
                  default=False,
                  help="Check dependencies only, do not download or build.")

        # Need to be able to look for an alternate build.config file.
        AddOption('--build-config',
                  dest='build_config',
                  default=os.path.join(Dir('#').abspath,
                                       'utils', 'build.config'),
                  help='build config file to use. [%default]')

        # We need to sometimes use alternate tools for building and need
        # to add them to the PATH in the environment.
        AddOption('--prepend-path',
                  dest='prepend_path',
                  default=None,
                  help="String to prepend to PATH environment variable.")

        # Allow specifying the locale to be used.  Default "en_US.UTF8"
        AddOption('--locale-name',
                  dest='locale_name',
                  default='en_US.UTF8',
                  help='locale to use for building. [%default]')

        SetOption("implicit_cache", True)

        self.add_opts(EnumVariable('WARNING_LEVEL', "Set default warning level",
                                   'error', ['warning', 'warn', 'error'],
                                   ignorecase=1))

    def __parse_build_deps(self):
        """Parse the build dependances command line flag"""
        build_deps = GetOption('build_deps')
        if build_deps == 'yes':
            self.download_deps = True
            self.build_deps = True
        elif build_deps == 'build-only':
            self.build_deps = True

    def setup_path_var(self, var, multiple=False):
        """Create a command line variable for a path"""
        tmp = self.__env.get(var)
        if tmp:
            realpath = lambda x: os.path.realpath(os.path.join(self.__top_dir, x))
            if multiple:
                value = os.pathsep.join(map(realpath, tmp.split(os.pathsep)))
            else:
                value = realpath(tmp)
            self.__env[var] = value
            self.__opts.args[var] = value

    def add_opts(self, *variables):
        """Add options to the command line"""
        for var in variables:
            self.__opts.Add(var)
        try:
            self.__opts.Update(self.__env)
        except UserError:
            if self.__dry_run:
                print('except on add_opts, self.__opts.Update')
            else:
                raise

    def define(self,
               name,
               **kw):
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
        """
        use_installed = False
        if 'all' in self.installed or name in self.installed:
            use_installed = True
        comp = _Component(self,
                          name,
                          use_installed,
                          **kw)
        self.__defined[name] = comp

    def load_definitions(self, **kw):
        """Load default definitions

        Keyword arguments:
            prebuild -- A list of components to prebuild
        """

        try:
            # pylint: disable=import-outside-toplevel
            from components import define_components
            # pylint: enable=import-outside-toplevel
            define_components(self)
        except Exception as old:
            raise BadScript("components", traceback.format_exc()) from old

        # Go ahead and prebuild some components

        prebuild = kw.get("prebuild", [])
        for comp in prebuild:
            env = self.__env.Clone()
            self.require(env, comp)

    def load_defaults(self, is_arm):
        """Setup default build parameters"""
        # argobots is not really needed by client but it's difficult to separate
        common_reqs = ['argobots', 'ofi', 'hwloc', 'mercury', 'boost', 'uuid',
                       'crypto', 'protobufc', 'lz4']
        client_reqs = ['fuse', 'json-c']
        server_reqs = ['pmdk']
        test_reqs = ['cmocka']

        if not is_arm:
            server_reqs.extend(['spdk'])
            common_reqs.extend(['isal', 'isal_crypto'])
        reqs = []
        if not self._build_targets:
            raise ValueError("Call init_build_targets before load_defaults")
        reqs = common_reqs
        if self.test_requested():
            reqs.extend(test_reqs)
        if self.server_requested():
            reqs.extend(server_reqs)
        if self.client_requested():
            reqs.extend(client_reqs)
        self.load_definitions(prebuild=reqs)

    def server_requested(self):
        """return True if server build is requested"""
        return "server" in self._build_targets

    def client_requested(self):
        """return True if client build is requested"""
        return "client" in self._build_targets

    def test_requested(self):
        """return True if test build is requested"""
        return "test" in self._build_targets

    def modify_prefix(self, comp_def, env):  # pylint: disable=unused-argument
        """Overwrite the prefix in cases where we may be using the default"""
        if comp_def.package:
            return
        prebuilt1 = os.path.join(self.prereq_prefix, comp_def.name)
        prebuilt2 = self.__env.get('{}_PREFIX'.format(comp_def.name.upper()))

        if comp_def.src_path and \
           not os.path.exists(comp_def.src_path) and \
           not os.path.exists(prebuilt1) and \
           not os.path.exists(prebuilt2):
            self.save_component_prefix('%s_PREFIX' %
                                       comp_def.name.upper(),
                                       "/usr")

    def require(self,
                env,
                *comps,
                **kw):
        """Ensure a component is built, if necessary, and add necessary
           libraries and paths to the construction environment.

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
                needed_libs = kw.get('%s_libs' % comp, comp_def.libs)
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
                    self.modify_prefix(comp_def, env)
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
            if comp not in OPTIONAL_COMPS:
                continue
            if not set([comp, 'all']).intersection(set(self.include)):
                return False
        return True

    def check_component(self, *comps, **kw):
        """Returns True if a component is available"""
        env = self.__env.Clone()
        try:
            self.require(env, *comps, **kw)
        except Exception as error:
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

    def replace_env(self, **kw):
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
            for lib in ['lib64', 'lib']:
                lpath = os.path.join(path, lib)
                if not os.path.exists(lpath):
                    lpath = None
            if ipath is None and lpath is None:
                continue
            env = self.__env.Clone()
            if ipath:
                env.AppendUnique(CPPPATH=[ipath])
            if lpath:
                env.AppendUnique(LIBPATH=[lpath])
            if not comp.has_missing_targets(env):
                self.__prebuilt_path[name] = path
                return path

        self.__prebuilt_path[name] = None

        return None

    def get_defined_components(self):
        """Get a list of all defined component names"""
        return copy.copy(list(self.__defined.keys()))

    def get_defined(self):
        """Get a dictionary of defined components"""
        return copy.copy(self.__defined)

    def get_component(self, name):
        """Get a component definition"""
        return self.__defined[name]

    def save_component_prefix(self, var, value):
        """Save the component prefix in the environment and
           in build info"""
        self.replace_env(**{var: value})
        self.__build_info.update(var, value)

    def get_prefixes(self, name, prebuilt_path):
        """Get the location of the scons prefix as well as the external component prefix."""
        prefix = self.__env.get('PREFIX')
        comp_prefix = '%s_PREFIX' % name.upper()
        if prebuilt_path:
            self.save_component_prefix(comp_prefix, prebuilt_path)
            return (prebuilt_path, prefix)

        target_prefix = os.path.join(self.prereq_prefix, name)
        self.save_component_prefix(comp_prefix, target_prefix)

        return (target_prefix, prefix)

    def get_src_build_dir(self):
        """Get the location of a temporary directory for hosting
           intermediate build files"""
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
        if self.configs is None:
            return None
        if not self.configs.has_section(section):
            return None

        if not self.configs.has_option(section, name):
            return None
        return self.configs.get(section, name)

    def load_config(self, comp, path):
        """If the component has a config file to load, load it"""
        config_path = self.get_config("configs", comp)
        if config_path is None:
            return
        full_path = "%s/%s" % (path, config_path)
        print("Reading config file for %s from %s" % (comp, full_path))
        self.configs.read(full_path)

# pylint: enable=too-many-public-methods


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
        self.patch_rpath = kw.get("patch_rpath", [])
        self.src_path = None
        self.prefix = None
        self.component_prefix = None
        self.package = kw.get("package", None)
        self.progs = kw.get("progs", [])
        self.libs = kw.get("libs", [])
        self.libs_cc = kw.get("libs_cc", None)
        self.functions = kw.get("functions", {})
        self.config_cb = kw.get("config_cb", None)
        self.required_libs = kw.get("required_libs", [])
        self.required_progs = kw.get("required_progs", [])
        if self.patch_rpath:
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
        self.crc_file = os.path.join(self.prereqs.get_build_dir(),
                                     '_%s.crc' % self.name)
        self.patch_path = self.prereqs.get_build_dir()

    def _delete_old_file(self, path):
        """delete the old file"""
        if os.path.exists(path):
            if self.__dry_run:
                print('Would unlink %s' % path)
            else:
                os.unlink(path)

    def resolve_patches(self):
        """parse the patches variable"""
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
            if "https://" not in raw:
                patches[raw] = patch_subdir
                continue
            patch_name = "%s_patch_%03d" % (self.name, patchnum)
            patch_path = os.path.join(self.patch_path, patch_name)
            patchnum += 1
            patches[patch_path] = patch_subdir
            if os.path.exists(patch_path):
                continue
            command = [['curl', '-sSfL', '--retry', '10', '--retry-max-time', '60',
                        '-o', patch_path, raw]]
            if not RUNNER.run_commands(command):
                raise BuildFailure(raw)
        return patches

    def get(self):
        """Download the component sources, if necessary"""
        if self.prebuilt_path:
            print('Using prebuilt binaries for %s' % self.name)
            return
        branch = self.prereqs.get_config("branches", self.name)
        commit_sha = self.prereqs.get_config("commit_versions", self.name)

        if not self.retriever:
            print('Using installed version of %s' % self.name)
            return

        # Source code is retrieved using retriever

        if not self.prereqs.download_deps:
            raise DownloadRequired(self.name)

        print('Downloading source for %s' % self.name)
        self._delete_old_file(self.crc_file)
        patches = self.resolve_patches()
        self.retriever.get(self.src_path, commit_sha=commit_sha,
                           patches=patches, branch=branch)

    def has_missing_system_deps(self, env):
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

        config = Configure(env)

        for lib in self.required_libs:
            if not config.CheckLib(lib):
                config.Finish()
                if self.__check_only:
                    env.SetOption('no_exec', True)
                return True

        try:
            for prog in self.required_progs:
                if isinstance(prog, ProgramBinary):
                    has_bin = prog.configure(config, self.prereqs)
                else:
                    has_bin = config.CheckProg(prog)
                if not has_bin:
                    config.Finish()
                    if self.__check_only:
                        env.SetOption('no_exec', True)
                    return True
        except AttributeError:
            # This feature is new in scons 2.4
            print('except AttributeError: new in scons 2.4')

        config.Finish()
        if self.__check_only:
            env.SetOption('no_exec', True)
        return False

    def parse_config(self, env, opts):
        """Parse a pkg-config file"""
        if self.pkgconfig is None:
            return False

        path = os.environ.get("PKG_CONFIG_PATH", None)
        if path and "PKG_CONFIG_PATH" not in env["ENV"]:
            env["ENV"]["PKG_CONFIG_PATH"] = path
        if self.component_prefix:
            for path in ["lib", "lib64"]:
                config = os.path.join(self.component_prefix, path, "pkgconfig")
                if not os.path.exists(config):
                    continue
                env.AppendENVPath("PKG_CONFIG_PATH", config)

        try:
            env.ParseConfig("pkg-config %s %s" % (opts, self.pkgconfig))
        except OSError:
            return True

        return False

    # pylint: disable=too-many-branches
    # pylint: disable=too-many-return-statements
    def has_missing_targets(self, env):
        """Check for expected build targets (e.g. libraries or headers)"""
        if self.targets_found:
            return False

        if self.__check_only:
            # Temporarily turn off dry-run.
            env.SetOption('no_exec', False)

        if env.GetOption('no_exec'):
            # Can not turn off dry run set by command line.
            print("Would check for missing build targets")
            return True

        # No need to fail here if we can't find the config, it may not always be generated
        self.parse_config(env, "--cflags")

        if GetOption('help'):
            return True

        config = Configure(env)
        if self.config_cb:
            if not self.config_cb(config):
                config.Finish()
                if self.__check_only:
                    env.SetOption('no_exec', True)
                return True

        for prog in self.progs:
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
                arg_key = "%s_PREFIX" % self.name.upper()
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
    # pylint: enable=too-many-branches
    # pylint: enable=too-many-return-statements

    def is_installed(self, needed_libs):
        """Check if the component is already installed"""
        if not self.use_installed:
            return False
        new_env = self.prereqs.system_env.Clone()
        self.set_environment(new_env, needed_libs)
        if self.has_missing_targets(new_env):
            self.use_installed = False
            return False
        return True

    def configure(self):
        """Setup paths for a required component"""
        if not self.retriever:
            self.prebuilt_path = "/usr"
        else:
            self.prebuilt_path = self.prereqs.get_prebuilt_path(self, self.name)

        (self.component_prefix, self.prefix) = self.prereqs.get_prefixes(self.name,
                                                                         self.prebuilt_path)
        self.src_path = None
        if self.retriever:
            self.src_path = self.prereqs.get_src_path(self.name)
        self.build_path = self.src_path
        if self.out_of_src_build:
            self.build_path = os.path.join(self.prereqs.get_build_dir(),
                                           '{}.build'.format(self.name))

            ensure_dir_exists(self.build_path, self.__dry_run)

    def set_environment(self, env, needed_libs):
        """Modify the specified construction environment to build with the external component"""
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
                # For binaries run during build
                env.AppendENVPath("LD_LIBRARY_PATH", full_path)

            # Ensure RUNPATH is used rather than RPATH.  RPATH is deprecated
            # and this allows LD_LIBRARY_PATH to override RPATH
            env.AppendUnique(LINKFLAGS=["-Wl,--enable-new-dtags"])
        if self.component_prefix == "/usr" and self.package is None:
            # hack until we have everything installed in lib64
            env.AppendUnique(RPATH=["/usr/lib"])
            env.AppendUnique(LINKFLAGS=["-Wl,--enable-new-dtags"])

        for define in self.defines:
            env.AppendUnique(CPPDEFINES=[define])

        self.parse_config(env, "--cflags")

        if needed_libs is None:
            return

        self.parse_config(env, "--libs")
        for path in lib_paths:
            env.AppendUnique(LIBPATH=[path])
        for lib in needed_libs:
            env.AppendUnique(LIBS=[lib])

    def check_installed_package(self, env):
        """Check installed targets"""
        if self.has_missing_targets(env):
            if self.__dry_run:
                if self.package is None:
                    print('Missing %s' % self.name)
                else:
                    print('Missing package %s %s' % (self.package, self.name))
                return
            if self.package is None:
                raise MissingTargets(self.name, self.name)

            raise MissingTargets(self.name, self.package)

    def check_user_options(self, env, needed_libs):
        """check help and clean options"""
        if GetOption('help'):
            if self.requires:
                self.prereqs.require(env, *self.requires)
            return True

        self.set_environment(env, needed_libs)
        if GetOption('clean'):
            return True
        return False

    def _rm_old_dir(self, path):
        """remove the old dir"""
        if self.__dry_run:
            print('Would empty %s' % path)
        else:
            shutil.rmtree(path)
            os.mkdir(path)

    def _has_changes(self):
        """check for changes"""
        has_changes = self.prereqs.build_deps
        if "all" in self.prereqs.installed:
            has_changes = False
        if self.name in self.prereqs.installed:
            has_changes = False
        if self.component_prefix and os.path.exists(self.component_prefix):
            has_changes = False

        return has_changes

    def _check_prereqs_build_deps(self):
        """check for build dependencies"""
        if not self.prereqs.build_deps:
            if self.__dry_run:
                print('Would do required build of %s' % self.name)
            else:
                raise BuildRequired(self.name)

    def patch_rpaths(self):
        """Run patchelf binary to add relative rpaths"""
        rpath = ["$$ORIGIN"]
        norigin = []
        comp_path = self.component_prefix
        if not comp_path or comp_path.startswith("/usr"):
            return
        if not os.path.exists(comp_path):
            return

        for libdir in ['lib64', 'lib']:
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
                    for libdir in ['lib64', 'lib']:
                        lpath = os.path.join(subpath, libdir)
                        if not os.path.exists(lpath):
                            continue
                        rpath.append(lpath)
                continue

            for libdir in ['lib64', 'lib']:
                path = os.path.join(rootpath, libdir)
                if not os.path.exists(path):
                    continue
                rpath.append("$$ORIGIN/../../%s/%s" % (prereq, libdir))
                norigin.append(os.path.normpath(path))
                break

        rpath += norigin
        for folder in self.patch_rpath:
            path = os.path.join(comp_path, folder)
            files = os.listdir(path)
            for lib in files:
                if not lib.endswith(".so"):
                    continue
                full_lib = os.path.join(path, lib)
                cmd = ['patchelf', '--set-rpath', ':'.join(rpath), full_lib]
                if not RUNNER.run_commands([cmd]):
                    print("Skipped patching %s" % full_lib)

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

        if self.check_user_options(env, needed_libs):
            return True
        self.set_environment(envcopy, self.libs)
        if self.prebuilt_path:
            self.check_installed_package(envcopy)
            return False

        # Default to has_changes = True which will cause all deps
        # to be built first time scons is invoked.
        has_changes = self._has_changes()

        if has_changes or self.has_missing_targets(envcopy):

            self._check_prereqs_build_deps()

            self.get()

            self.prereqs.load_config(self.name, self.src_path)

            if self.requires:
                changes = self.prereqs.require(envcopy, *self.requires,
                                               needed_libs=None)
                self.set_environment(envcopy, self.libs)

            if self.has_missing_system_deps(self.prereqs.system_env):
                raise MissingSystemLibs(self.name)

            ensure_dir_exists(self.prereqs.prereq_prefix, self.__dry_run)
            changes = True
            if self.out_of_src_build:
                self._rm_old_dir(self.build_path)
            if not RUNNER.run_commands(self.build_commands, subdir=self.build_path, env=envcopy):
                raise BuildFailure(self.name)

        # set environment one more time as new directories may be present
        if self.requires:
            self.prereqs.require(envcopy, *self.requires, needed_libs=None)
        self.set_environment(envcopy, self.libs)
        if changes:
            self.patch_rpaths()
        if self.has_missing_targets(envcopy) and not self.__dry_run:
            raise MissingTargets(self.name, None)
        return changes


__all__ = ["GitRepoRetriever", "WebRetriever",
           "DownloadFailure", "ExtractionError",
           "UnsupportedCompression", "BadScript",
           "MissingPath", "BuildFailure",
           "MissingDefinition", "MissingTargets",
           "MissingSystemLibs", "DownloadRequired",
           "PreReqComponent", "BuildRequired",
           "ProgramBinary"]

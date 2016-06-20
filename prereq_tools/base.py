#!/usr/bin/python
# -*- coding: utf-8 -*-
"""Classes for building external prerequisite components"""

# pylint: disable=too-few-public-methods
# pylint: disable=too-many-instance-attributes
# pylint: disable=broad-except
# pylint: disable=bare-except
# pylint: disable=exec-used
# pylint: disable=bad-builtin
# pylint: disable=too-many-statements

import os
import traceback
import hashlib
import subprocess
import socket
import tarfile
import re
import copy
from SCons.Variables import PathVariable
from SCons.Script import Dir
from SCons.Script import GetOption
from SCons.Script import Configure
from SCons.Script import AddOption
from SCons.Script import Builder
from build_info import BuildInfo

class NotInitialized(Exception):
    """Exception raised when classes used before initialization
    """

    def __init__(self):
        super(NotInitialized, self).__init__()

    def __str__(self):
        """ Exception string """
        return "PreReqComponents was not initialized"

class DownloadFailure(Exception):
    """Exception raised when source can't be downloaded

    Attributes:
        repo      -- Specified location
        component -- Component
    """

    def __init__(self, repo, component):
        super(DownloadFailure, self).__init__()
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
        super(ExtractionError, self).__init__()
        self.component = component

    def __str__(self):
        """ Exception string """

        return "Failed to extract %s" % (self.component)

class UnsupportedCompression(Exception):
    """Exception raised when library doesn't support extraction method

    Attributes:
        component -- Component
    """

    def __init__(self, component):
        super(UnsupportedCompression, self).__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "Don't know how to extract %s"%(self.component)

class BadScript(Exception):
    """Exception raised when a preload script has errors

    Attributes:
        script -- path to script
        trace  -- traceback
    """

    def __init__(self, script, trace):
        super(BadScript, self).__init__()
        self.script = script
        self.trace = trace

    def __str__(self):
        """ Exception string """
        return "Failed to execute %s:\n%s %s\n\nTraceback"%(self.script,
                                                            self.script,
                                                            self.trace)

class MissingDefinition(Exception):
    """Exception raised when component has no definition

    Attributes:
        component    -- component that is missing definition
    """

    def __init__(self, component):
        super(MissingDefinition, self).__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "No definition for %s"%(self.component)

class MissingPath(Exception):
    """Exception raised when user speficies a path that doesn't exist

    Attributes:
        variable    -- Variable specified
    """

    def __init__(self, variable):
        super(MissingPath, self).__init__()
        self.variable = variable

    def __str__(self):
        """ Exception string """
        return "%s specifies a path that doesn't exist"%(self.variable)


class BuildFailure(Exception):
    """Exception raised when component fails to build

    Attributes:
        component    -- component that failed to build
    """

    def __init__(self, component):
        super(BuildFailure, self).__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s failed to build"%(self.component)

class MissingTargets(Exception):
    """Exception raised when expected targets missing after component build

    Attributes:
        component    -- component that has missing targets
    """

    def __init__(self, component):
        super(MissingTargets, self).__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s has missing targets after build.  " \
               "See config.log for details"%(self.component)

class MissingSystemLibs(Exception):
    """Exception raised when libraries required for target build are not met

    Attributes:
        component    -- component that has missing targets
    """

    def __init__(self, component):
        super(MissingSystemLibs, self).__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s has unmet dependancies required for build"%(self.component)

class DownloadRequired(Exception):
    """Exception raised when component is missing but downloads aren't enabled

    Attributes:
        component    -- component that is missing
    """

    def __init__(self, component):
        super(DownloadRequired, self).__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s needs to be built, use --build-deps=yes"%(self.component)

class BuildRequired(Exception):
    """Exception raised when component is missing but builds aren't enabled

    Attributes:
        component    -- component that is missing
    """

    def __init__(self, component):
        super(BuildRequired, self).__init__()
        self.component = component

    def __str__(self):
        """ Exception string """
        return "%s needs to be built, use --build-deps=yes"%(self.component)

class Runner(object):
    """Runs commands in a specified environment"""
    def __init__(self):
        self.env = None

    def initialize(self, env):
        """Initialize the environment in the runner"""
        self.env = env

    def run_commands(self, commands, subdir=None):
        """Runs a set of commands in specified directory"""
        if not self.env:
            raise NotInitialized()
        retval = True
        old = os.getcwd()
        if subdir:
            os.chdir(subdir)
        print 'Running commands in %s' % os.getcwd()
        for command in commands:
            command = self.env.subst(command)
            print 'RUN: %s' % command
            if subprocess.call(command, shell=True, env=self.env['ENV']) != 0:
                retval = False
                break
        if subdir:
            os.chdir(old)
        return retval

RUNNER = Runner()

def check_test(target, source, env, mode):
    """Check the results of the test"""
    error_str = ""
    with open(target[0].path, "r") as fobj:
        for line in fobj.readlines():
            if re.search("FAILED", line):
                error_str = """
Please see %s for the errors and fix
the issues causing the TESTS to fail.
"""%target[0].path
                break
        fobj.close()
    if mode == "memcheck" or mode == "helgrind":
        from xml.etree import ElementTree
        for fname in target:
            if str(fname).endswith(".xml"):
                with open(str(fname), "r") as xmlfile:
                    tree = ElementTree.parse(xmlfile)
                error_types = {}
                for node in tree.iter('error'):
                    kind = node.find('./kind')
                    if not kind.text in error_types:
                        error_types[kind.text] = 0
                    error_types[kind.text] += 1
                if error_types:
                    error_str += """
Valgrind %s check failed.  See %s:"""%(mode, str(fname))
                    for err in sorted(error_types.keys()):
                        error_str += "\n%-3d %s errors"%(error_types[err],
                                                         err)
    if error_str:
        return """
#########################################################
Libraries built successfully but some unit TESTS failed.
%s
#########################################################
"""%error_str
    return None

def define_check_test(mode=None):
    """Define a function to create test checker"""
    return lambda target, source, env: check_test(target, source, env, mode)

def run_test(source, target, env, for_signature, mode=None):
    """Create test actions."""
    count = 1
    action = ['touch %s'%target[0]]
    for test in source:
        valgrind_str = ""
        if mode in ["memcheck", "helgrind"]:
            valgrind_str = "valgrind --xml=yes --xml-file=%s " \
                           "--child-silent-after-fork=yes " % target[count]
            if mode == "memcheck":
                #Memory analysis
                valgrind_str += "--leak-check=full "
            elif mode == "helgrind":
                #Thread analysis
                valgrind_str += "--tool=helgrind "
        count += 1
        action.append("%s%s >> %s"%(valgrind_str,
                                    str(test),
                                    target[0]))
    action.append("cat %s"%target[0])
    action.append(define_check_test(mode=mode))
    return action

def modify_targets(target, source, env, mode=None):
    """Emit the target list for the unit test builder"""
    target = ["test_output"]
    if mode == "memcheck" or mode == "helgrind":
        for src in source:
            basename = os.path.basename(str(src))
            xml = "%s-%s.xml"%(mode, basename)
            target.append(xml)
    return target, source

def define_run_test(mode=None):
    """Define a function to create test actions"""
    return lambda source, target, env, for_signature: run_test(source,
                                                               target,
                                                               env,
                                                               for_signature,
                                                               mode)

def define_modify_targets(mode=None):
    """Define a function to create test targets"""
    return lambda target, source, env: modify_targets(target, source,
                                                      env, mode)

class GitRepoRetriever(object):
    """Identify a git repository from which to download sources"""

    def __init__(self, url, has_submodules=False):

        self.url = url
        self.has_submodules = has_submodules

    def update_submodules(self, subdir):
        """ update the git submodules """
        if self.has_submodules:
            commands = ['git submodule init', 'git submodule update']
            if not RUNNER.run_commands(commands, subdir=subdir):
                raise DownloadFailure(self.url, subdir)

    def get(self, subdir):
        """Downloads sources from a git repository into subdir"""
        commands = ['git clone %s %s' % (self.url, subdir)]
        if not RUNNER.run_commands(commands):
            raise DownloadFailure(self.url, subdir)
        self.update_submodules(subdir)

    def update(self, subdir):
        """ update a repository """
        commands = ['git pull origin master']
        if not RUNNER.run_commands(commands, subdir=subdir):
            raise DownloadFailure(self.url, subdir)
        self.update_submodules(subdir)

class WebRetriever(object):
    """Identify a location from where to download a source package"""

    def __init__(self, url):
        self.url = url

    def get(self, subdir):
        """Downloads and extracts sources from a url into subdir"""
        basename = os.path.basename(self.url)

        if os.path.exists(basename) and os.path.exists(subdir):
            #assume that nothing has changed
            return

        commands = ['rm -rf %s' % subdir]
        if not os.path.exists(basename):
            commands.append('curl -O %s' % self.url)

        if not RUNNER.run_commands(commands):
            raise DownloadFailure(self.url, subdir)

        if self.url.endswith('.tar.gz') or self.url.endswith('.tgz'):
            tfile = tarfile.open(basename, 'r:gz')
            members = tfile.getnames()
            prefix = os.path.commonprefix(members)
            tfile.extractall()
            if not RUNNER.run_commands(['mv %s %s' % (prefix, subdir)]):
                raise ExtractionError(subdir)
        else:
            raise UnsupportedCompression(subdir)

    def update(self, subdir):
        """ update the code if the url has changed """
        # Will download the file if the name has changed
        self.get(subdir)

class PreReqComponent(object):
    """A class for defining and managing external components required
       by a project.

    If provided arch is a string to embed in any generated directories
    to allow compilation from from multiple systems in one source tree
    """

    def __init__(self, env, variables, arch=None):
        self.__defined = {}
        self.__required = {}
        self.__env = env
        self.__opts = variables
        self.system_env = env.Clone()

        real_env = self.__env['ENV']

        auth_sock = os.environ.get('SSH_AUTH_SOCK')
        if auth_sock:
            real_env['SSH_AUTH_SOCK'] = auth_sock

        libtoolize = 'libtoolize'
        if self.__env['PLATFORM'] == 'darwin':
            libtoolize = 'glibtoolize'

        real_env["HOME"] = os.environ.get("HOME")
        term = os.environ.get("TERM")
        if term:
            real_env["TERM"] = term
        try:
            socket.gethostbyname('proxy-chain.intel.com')
            real_env['http_proxy'] = 'http://proxy-chain.intel.com:911'
            real_env['https_proxy'] = \
                'https://proxy-chain.intel.com:912'
        except Exception:
            # Not on Intel network
            pass

        real_env["PYTHONPATH"] = Dir("#").abspath + os.pathsep + \
                                 os.environ.get("PYTHONPATH", "")

        self.add_options()
        self.__setup_unit_test_builders()
        self.__update = GetOption('update_prereq')
        self.download_deps = False
        self.build_deps = False
        self.__parse_build_deps()
        self.replace_env(LIBTOOLIZE=libtoolize)
        self.__env.Replace(ENV=real_env)

        RUNNER.initialize(self.__env)

        self.__top_dir = Dir('#').abspath

        build_dir_name = '_build.external'
        install_dir = os.path.join(self.__top_dir, 'install')
        if arch:
            build_dir_name = '_build.external-%s'%arch
            install_dir = os.path.join('install', str(arch))

            # Overwrite default file locations to allow multiple builds in the
            # same tree.
            env.SConsignFile('.sconsign-%s' % arch)
            env.Replace(CONFIGUREDIR='#/.scons-temp-%s' % arch,
                        CONFIGURELOG='#/config-%s.log' % arch)

        self.__build_dir = os.path.realpath(os.path.join(self.__top_dir,
                                                         build_dir_name))
        try:
            os.makedirs(self.__build_dir)
        except Exception:
            pass
        self.__prebuilt_path = {}
        self.__src_path = {}

        self.add_opts(PathVariable('PREFIX', 'Installation path', install_dir,
                                   PathVariable.PathIsDirCreate),
                      ('PREBUILT_PREFIX',
                       'Colon separated list of paths to look for prebuilt '
                       'components.',
                       None),
                      ('SRC_PREFIX',
                       'Colon separated list of paths to look for source '
                       'of prebuilt components.',
                       None),
                      PathVariable('TARGET_PREFIX',
                                   'Installation root for prebuilt components',
                                   None, PathVariable.PathIsDirCreate))
        self.setup_path_var('PREFIX')
        self.setup_path_var('PREBUILT_PREFIX', True)
        self.setup_path_var('TARGET_PREFIX')
        self.setup_path_var('SRC_PREFIX', True)
        self.setup_patch_prefix()
        self.__build_info = BuildInfo()
        self.__build_info.update("PREFIX", self.__env.subst("$PREFIX"))

    def get_build_info(self):
        """Retrieve the BuildInfo"""
        return self.__build_info

    @staticmethod
    def add_options():
        """Add common options to environment"""

        AddOption('--update-prereq',
                  dest='update_prereq',
                  type='string',
                  nargs=1,
                  action='append',
                  default=[],
                  metavar='COMPONENT',
                  help='Force an update of a prerequisite COMPONENT.  Use '
                       '\'all\' to update all components')

        # There is another case here which isn't handled, we want Jenkins builds
        # to download tar packages but not git source code.  For now just have a
        # single flag and set this for the Jenkins builds which need this.
        AddOption('--build-deps',
                  dest='build_deps',
                  type='choice',
                  choices=['yes', 'no', 'build-only'],
                  default='no',
                  help="Automatically download and build sources")


    def setup_patch_prefix(self):
        """Discovers the location of the patches directory and adds it to
           the construction environment."""
        patch_dirs = [os.path.join(self.__top_dir,
                                   "scons_local", "sl_patches"),
                      os.path.join(self.__top_dir, "sl_patches")]
        for patch_dir in patch_dirs:
            if os.path.exists(patch_dir):
                self.replace_env(**{'PATCH_PREFIX': patch_dir})
                break

    def __setup_unit_test_builders(self):
        """Setup unit test builders for general use"""
        AddOption('--utest-mode',
                  dest='utest_mode',
                  type='choice',
                  choices=['native', 'memcheck', 'helgrind'],
                  default='native',
                  help="Specifies mode for running unit tests")

        mode = GetOption("utest_mode")
        test_run = Builder(generator=define_run_test(mode),
                           emitter=define_modify_targets(mode))

        self.__env.Append(BUILDERS={"RunTests" : test_run})


    def __parse_build_deps(self):
        """Parse the build dependances command line flag"""
        build_deps = GetOption('build_deps')
        if build_deps == 'yes':
            self.download_deps = True
            self.build_deps = True
        elif build_deps == 'build-only':
            self.build_deps = True

        # If the --update-prereq option is given then it is OK to build
        # code but not to download it unless --build-deps=yes is also
        # given.
        if self.__update:
            self.build_deps = True

    def setup_path_var(self, var, multiple=False):
        """Create a command line variable for a path"""
        tmp = self.__env.get(var)
        if tmp:
            realpath = lambda x: os.path.realpath(os.path.join(self.__top_dir,
                                                               x))
            if multiple:
                value = os.pathsep.join(map(realpath, tmp.split(os.pathsep)))
            else:
                value = realpath(tmp)
            self.__env[var] = value
            self.__opts.args[var] = value

    def update_src_path(self, name, value):
        """Update a variable in the default construction environment"""
        opt_name = '%s_SRC' % name.upper()
        self.__env[opt_name] = value

    def add_opts(self, *variables):
        """Add options to the command line"""
        for var in variables:
            self.__opts.Add(var)
        self.__opts.Update(self.__env)

    def define(self,
               name,
               **kw
              ):
        """Define an external prerequisite component

    Args:
        name -- The name of the component definition

    Keyword arguments:
        libs -- A list of libraries to add to dependent components
        headers -- A list of expected headers
        requires -- A list of names of required component definitions
        required_libs -- A list of system libraries to be checked for
        defines -- Defines needed to use the component
        commands -- A list of commands to run to build the component
        retriever -- A retriever object to download component
        extra_lib_path -- Subdirectories to add to dependent component path
        extra_include_path -- Subdirectories to add to dependent component path
        out_of_src_build -- Build from a different directory if set to True
        """
        update = False
        if 'all' in self.__update or name in self.__update:
            update = True
        comp = _Component(self,
                          name,
                          update,
                          **kw
                         )
        self.__defined[name] = comp

    def preload(self, script, **kw):
        r"""Execute a script to define external components

        Args:
            script -- The script to execute

        Keyword arguments:
            prebuild -- A list of components to prebuild
        """

        try:
            gvars = {'prereqs': self}
            lvars = {}
            sfile = open(script, 'r')
            scomp = compile(sfile.read(), '<string>', 'exec')
            exec (scomp, gvars, lvars)
        except Exception:
            raise BadScript(script, traceback.format_exc())

        # Go ahead and prebuild some components

        prebuild = kw.get("prebuild", [])
        for comp in prebuild:
            env = self.__env.Clone()
            self.require(env, comp)

    def require(self,
                env,
                *comps,
                **kw
               ):
        """Ensure a component is built, if necessary, and add necessary
           libraries and paths to the construction environment.

        Args:
            env -- The construction environment to modify
            comps -- component names to configure
            kw -- Keyword arguments

        Keyword arguments:
            headers_only -- if set to True, skip library configuration
        """
        changes = False
        headers_only = kw.get('headers_only', False)
        for comp in comps:
            if comp in self.__required:
                if GetOption('help'):
                    continue
                # checkout and build done previously
                self.set_environment_only(env,
                                          self.__defined[comp],
                                          headers_only)
                if GetOption('clean'):
                    continue
                if self.__required[comp]:
                    changes = True
                continue
            if not comp in self.__defined:
                raise MissingDefinition(comp)
            self.__required[comp] = False
            self.__defined[comp].configure()
            if self.__defined[comp].build(env, headers_only):
                self.__required[comp] = False
                changes = True

        return changes

    def set_environment_only(self, env, comp, headers_only):
        """Recursively set the environment for a previously built component"""
        comp.set_environment(env, headers_only)
        # Now add the headers only for dependencies
        for name in comp.requires:
            self.set_environment_only(env,
                                      self.__defined[name],
                                      True)

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

    def get_prebuilt_path(self, name, prebuilt_default):
        """Get the path for a prebuilt component"""
        if name in self.__prebuilt_path:
            return self.__prebuilt_path[name]

        opt_name = '%s_PREBUILT' % name.upper()
        self.add_opts(PathVariable(opt_name,
                                   'Alternate installation '
                                   'prefix for %s' % name,
                                   prebuilt_default, PathVariable.PathIsDir))
        self.setup_path_var(opt_name)
        prebuilt = self.__env.get(opt_name)
        if prebuilt and not os.path.exists(prebuilt):
            raise MissingPath(opt_name)

        if not prebuilt:
            # check the global prebuilt area
            prebuilt_path = self.__env.get('PREBUILT_PREFIX')
            if prebuilt_path:
                for path in prebuilt_path.split(os.pathsep):
                    prebuilt = os.path.join(path, name)
                    if os.path.exists(prebuilt):
                        break
                    prebuilt = None

        self.__prebuilt_path[name] = prebuilt
        return prebuilt

    def get_defined_components(self):
        """Get a list of all defined components"""
        return copy.copy(self.__defined.keys())

    def save_component_prefix(self, var, value):
        """Save the component prefix in the environment and
           in build info"""
        self.replace_env(**{var:value})
        self.__build_info.update(var, value)

    def get_prefixes(self, name, prebuilt_path):
        """Get the location of the scons prefix as well as the external
           component prefix."""
        prefix = self.__env.get('PREFIX')
        comp_prefix = '%s_PREFIX' % name.upper()
        if prebuilt_path:
            self.save_component_prefix(comp_prefix, prebuilt_path)
            return (prebuilt_path, prefix)
        target_prefix = self.__env.get('TARGET_PREFIX')
        if target_prefix:
            target_prefix = os.path.join(target_prefix, name)
            self.save_component_prefix(comp_prefix, target_prefix)
            return (target_prefix, prefix)
        self.save_component_prefix(comp_prefix, prefix)
        return (prefix, prefix)

    def get_src_path(self, name):
        """Get the location of the sources for an external component"""
        if name in self.__src_path:
            return self.__src_path[name]
        opt_name = '%s_SRC' % name.upper()
        self.add_opts(PathVariable(opt_name,
                                   'Alternate path for %s source' % name, None,
                                   PathVariable.PathIsDir))
        self.setup_path_var(opt_name)

        src_path = self.__env.get(opt_name)
        if src_path and not os.path.exists(src_path):
            raise MissingPath(opt_name)

        if not src_path:

            # check the global source area

            src_path_var = self.__env.get('SRC_PREFIX')
            if src_path_var:
                for path in src_path_var.split(os.pathsep):
                    src_path = os.path.join(path, name)
                    if os.path.exists(src_path):
                        break
                    src_path = None

        if not src_path:
            src_path = os.path.join(self.__build_dir, name)

        self.__src_path[name] = src_path
        return src_path


class _Component(object):
    """A class to define attributes of an external component

    Args:
        prereqs -- A PreReqComponent object
        name -- The name of the component definition

    Keyword arguments:
        libs -- A list of libraries to add to dependent components
        headers -- A list of expected headers
        requires -- A list of names of required component definitions
        commands -- A list of commands to run to build the component
        retriever -- A retriever object to download component
        extra_lib_path -- Subdirectories to add to dependent component path
        extra_include_path -- Subdirectories to add to dependent component path
        out_of_src_build -- Build from a different directory if set to True
    """

    def __init__(self,
                 prereqs,
                 name,
                 update,
                 **kw
                ):

        self.targets_found = False
        self.update = update
        self.build_path = None
        self.prebuilt_path = None
        self.src_path = None
        self.prefix = None
        self.component_prefix = None
        self.libs = kw.get("libs", [])
        self.required_libs = kw.get("required_libs", [])
        self.required_progs = kw.get("required_progs", [])
        self.defines = kw.get("defines", [])
        self.headers = kw.get("headers", [])
        self.requires = kw.get("requires", [])
        self.prereqs = prereqs
        self.name = name
        self.build_commands = kw.get("commands", [])
        self.retriever = kw.get("retriever", None)
        self.lib_path = ['lib']
        self.include_path = ['include']
        extra_lib_path = kw.get("extra_lib_path", [])
        if extra_lib_path:
            for path in extra_lib_path:
                self.lib_path.append(path)
        extra_include_path = kw.get("extra_include_path", [])
        if extra_include_path:
            for path in extra_include_path:
                self.include_path.append(path)
        self.out_of_src_build = kw.get("out_of_src_build", False)
        self.src_opt = '%s_SRC' % name.upper()
        self.prebuilt_opt = '%s_PREBUILT' % name.upper()
        self.crc_file = os.path.join(self.prereqs.get_build_dir(),
                                     '_%s.crc' % self.name)

    def src_exists(self):
        """Check if the source directory exists"""
        if self.src_path and os.path.exists(self.src_path):
            return True
        return False

    def get(self):
        """Download the component sources, if necessary"""
        if self.prebuilt_path:
            print 'Using prebuilt binaries for %s' % self.name
            return
        if self.src_exists():
            self.prereqs.update_src_path(self.name, self.src_path)
            print 'Using existing sources at %s for %s' \
                % (self.src_path, self.name)
            if self.update:
                defpath = os.path.join(self.prereqs.get_build_dir(), self.name)
                #only do this if the source was checked out by this script
                if self.src_path == defpath:
                    self.retriever.update(self.src_path)
            return
        if not self.retriever:
            print 'Using installed version of %s' % self.name
            return

        # Source code is retrieved using retriever

        if not self.prereqs.download_deps:
            raise DownloadRequired(self.name)

        print 'Downloading source for %s' % self.name
        if os.path.exists(self.crc_file):
            os.unlink(self.crc_file)
        self.retriever.get(self.src_path)
        self.prereqs.update_src_path(self.name, self.src_path)

    def calculate_crc(self):
        """Calculate a CRC on the sources to detect changes"""
        new_crc = ''
        if not self.src_path:
            return new_crc
        for (root, _, files) in os.walk(self.src_path):
            for fname in files:
                (_, ext) = os.path.splitext(fname)

                 # not fool proof but may be good enough

                if ext in ['.c',
                           '.h',
                           '.cpp',
                           '.cc',
                           '.hpp',
                           '.ac',
                           '.in',
                           '.py',
                          ]:
                    with open(os.path.join(root, fname), 'r') as src:
                        new_crc += hashlib.md5(src.read()).hexdigest()
        return new_crc

    def has_changes(self):
        """Check the sources for changes since the last build"""

        old_crc = ''
        try:
            with open(self.crc_file, 'r') as crcfile:
                old_crc = crcfile.read()
        except IOError:
            pass
        if old_crc == '':
            return True
        #only do CRC check if the sources have been updated
        if self.update:
            print 'Checking %s sources for changes' % self.name
            if old_crc != self.calculate_crc():
                return True
        return False

    def has_missing_system_deps(self, env):
        """Check for required system libs"""

        config = Configure(env)

        for lib in self.required_libs:
            if not config.CheckLib(lib):
                config.Finish()
                return True

        try:
            for prog in self.required_progs:
                if not config.CheckProg(prog):
                    config.Finish()
                    return True
        except AttributeError:
            # This feature is new in scons 2.4
            pass

        config.Finish()
        return False

    def has_missing_targets(self, env):
        """Check for expected build targets (e.g. libraries or headers)"""

        if self.targets_found:
            return False

        config = Configure(env)
        for header in self.headers:
            if not config.CheckHeader(header):
                config.Finish()
                return True

        for lib in self.libs:
            if not config.CheckLib(lib):
                config.Finish()
                return True

        config.Finish()
        self.targets_found = True
        return False

    def configure(self):
        """Setup paths for a required component"""
        self.prereqs.setup_path_var(self.src_opt)
        self.prereqs.setup_path_var(self.prebuilt_opt)
        prebuilt_default = None
        if not self.retriever:
            prebuilt_default = "/usr"
        self.prebuilt_path = self.prereqs.get_prebuilt_path(self.name,
                                                            prebuilt_default)

        (self.component_prefix, self.prefix) = \
            self.prereqs.get_prefixes(self.name, self.prebuilt_path)
        self.src_path = None
        if self.retriever:
            self.src_path = self.prereqs.get_src_path(self.name)
        self.build_path = self.src_path
        if self.out_of_src_build:
            self.build_path = \
                os.path.join(self.prereqs.get_build_dir(), '%s.build'
                             % self.name)
            try:
                os.makedirs(self.build_path)
            except:
                pass

    def set_environment(self, env, headers_only):
        """Modify the specified construction environment to build with
           the external component"""
        for path in self.include_path:
            env.AppendUnique(CPPPATH=[os.path.join(self.component_prefix,
                                                   path)])
        # The same rules that apply to headers apply to RPATH.   If a build
        # uses a component, that build needs the RPATH of the dependencies.
        for path in self.lib_path:
            env.AppendUnique(RPATH=[os.path.join(self.component_prefix, path)])

        for define in self.defines:
            env.AppendUnique(CPPDEFINES=[define])

        if headers_only:
            return

        for path in self.lib_path:
            env.AppendUnique(LIBPATH=[os.path.join(self.component_prefix,
                                                   path)])
        for lib in self.libs:
            env.AppendUnique(LIBS=[lib])

    def build(self, env, headers_only):
        """Build the component, if necessary"""
        # Ensure requirements are met
        changes = False
        if self.requires:
            changes = self.prereqs.require(env, *self.requires,
                                           headers_only=True)
        envcopy = env.Clone()

        if GetOption('help'):
            return
        self.set_environment(env, headers_only)
        if GetOption('clean'):
            return
        self.set_environment(envcopy, False)
        if self.prebuilt_path:
            return False

        # Default to has_changes = True which will cause all deps
        # to be built first time scons is invoked.
        has_changes = True
        if self.src_exists():
            self.get()
            has_changes = self.has_changes()

        if changes or has_changes \
            or self.has_missing_targets(envcopy):

            if not self.prereqs.build_deps:
                raise BuildRequired(self.name)

            if self.has_missing_system_deps(self.prereqs.system_env):
                raise MissingSystemLibs(self.name)

            if not self.src_exists():
                self.get()
            changes = True
            if has_changes and self.out_of_src_build:
                os.system("rm -rf %s"%self.build_path)
                os.mkdir(self.build_path)
            if not RUNNER.run_commands(self.build_commands,
                                       subdir=self.build_path):
                raise BuildFailure(self.name)

        if self.has_missing_targets(envcopy):
            raise MissingTargets(self.name)
        new_crc = self.calculate_crc()
        with open(self.crc_file, 'w') as crcfile:
            crcfile.write(new_crc)
        return changes

__all__ = ["GitRepoRetriever", "WebRetriever",
           "PreReqComponent", "NotInitialized",
           "DownloadFailure", "ExtractionError",
           "UnsupportedCompression", "BadScript",
           "MissingPath", "BuildFailure",
           "MissingDefinition", "MissingTargets",
           "MissingSystemLibs", "DownloadRequired",
           "BuildRequired"
          ]

#!/usr/bin/python
# -*- coding: utf-8 -*-
"""Classes for building external prerequisite components"""

# pylint: disable=too-few-public-methods
# pylint: disable=too-many-instance-attributes
# pylint: disable=broad-except
# pylint: disable=bare-except
# pylint: disable=exec-used
import os
import traceback
import hashlib
import subprocess
import socket
import tarfile
from SCons.Variables import PathVariable
from SCons.Script import Dir
from SCons.Script import GetOption
from SCons.Script import Configure
from SCons.Script import AddOption

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
        return "%s has missing targets after build"%(self.component)

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
       by a project."""

    def __init__(self, env, variables):
        self.__defined = {}
        self.__required = {}
        self.__env = env
        self.__opts = variables

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

        AddOption('--update-prereq',
                  dest='update_prereq',
                  type='string',
                  nargs=1,
                  action='append',
                  default=[],
                  metavar='COMPONENT',
                  help='Force an update of a prerequisite COMPONENT.  Use '
                       '\'all\' to update all components')
        self.__update = GetOption('update_prereq')
        self.replace_env(LIBTOOLIZE=libtoolize)
        self.__env.Replace(ENV=real_env)

        RUNNER.initialize(self.__env)

        self.__top_dir = Dir('#').abspath
        self.__build_dir = \
            os.path.realpath(os.path.join(self.__top_dir, '_build.external'))
        try:
            os.makedirs(self.__build_dir)
        except Exception:
            pass
        self.__prebuilt_path = {}
        self.__src_path = {}

        tmp = os.path.join(self.__top_dir, 'install')
        self.add_opts(PathVariable('PREFIX', 'Installation path', tmp,
                                   PathVariable.PathIsDirCreate),
                      PathVariable('PREBUILT_PREFIX',
                                   'Directory to look for prebuilt components',
                                   None, PathVariable.PathIsDir),
                      PathVariable('SRC_PREFIX',
                                   'Default directory to look for component '
                                   'sources',
                                   None, PathVariable.PathIsDir),
                      PathVariable('TARGET_PREFIX',
                                   'Installation root for prebuilt components',
                                   None, PathVariable.PathIsDirCreate))
        self.setup_path_var('PREFIX')
        self.setup_path_var('PREBUILT_PREFIX')
        self.setup_path_var('TARGET_PREFIX')
        self.setup_path_var('SRC_PREFIX')

    def setup_path_var(self, var):
        """Create a command line variable for a path"""
        tmp = self.__env.get(var)
        if tmp:
            value = os.path.realpath(os.path.join(self.__top_dir, tmp))
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
                self.__defined[comp].set_environment(env, headers_only)
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

            prebuilt = self.__env.get('PREBUILT_PREFIX')
            if prebuilt:
                prebuilt = os.path.join(prebuilt, name)
                if not os.path.exists(prebuilt):
                    prebuilt = None

        self.__prebuilt_path[name] = prebuilt
        return prebuilt

# pylint: disable=star-args
    def get_prefixes(self, name, prebuilt_path):
        """Get the location of the scons prefix as well as the external
           component prefix."""
        prefix = self.__env.get('PREFIX')
        if prebuilt_path:
            self.replace_env(**{'%s_PREFIX' \
                             % name.upper(): prebuilt_path})
            return (prebuilt_path, prefix)
        target_prefix = self.__env.get('TARGET_PREFIX')
        if target_prefix:
            target_prefix = os.path.join(target_prefix, name)
            self.replace_env(**{'%s_PREFIX' \
                             % name.upper(): target_prefix})
            return (target_prefix, prefix)
        self.replace_env(**{'%s_PREFIX' % name.upper(): prefix})
        return (prefix, prefix)
# pylint: enable=star-args

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

            src_path = self.__env.get('SRC_PREFIX')
            if src_path:
                src_path = os.path.join(src_path, name)
                if not os.path.exists(src_path):
                    print 'No source for %s found in SRC_PREFIX' % name
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
        self.required_libs = []
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

    def get(self):
        """Download the component sources, if necessary"""
        if self.prebuilt_path:
            print 'Using prebuilt binaries for %s' % self.name
            return
        if self.src_path and os.path.exists(self.src_path):
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

        for lib in self.required_libs:
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
            env.Append(CPPPATH=[os.path.join(self.component_prefix, path)])

        if headers_only:
            return

        for path in self.lib_path:
            env.Append(LIBPATH=[os.path.join(self.component_prefix, path)])
        for lib in self.libs:
            env.Append(LIBS=[lib])

    def create_links(self, source):
        """Create symbolic links to real targets in $PREFIX"""
        if self.retriever == None:
            #Don't do this for installed components
            return
        if source == self.prefix:
            return
        # create links
        for (root, _, files) in os.walk(source):
            local_root = root.replace(source,
                                      self.prefix)
            if not os.path.exists(local_root):
                try:
                    os.makedirs(local_root)
                except:
                    pass
            for fname in files:
                sfile = os.path.join(root, fname)
                target = os.path.join(local_root, fname)
                try:
                    if os.path.exists(target):
                        os.unlink(target)
                    os.symlink(sfile, target)
                except OSError:
                    pass


    def build(self, env, headers_only):
        """Build the component, if necessary"""
        envcopy = env.Clone()
        # Ensure requirements are met

        changes = self.prereqs.require(envcopy, *self.requires)
        if GetOption('help'):
            return
        self.set_environment(env, headers_only)
        self.set_environment(envcopy, False)
        self.get()
        if self.prebuilt_path:
            self.create_links(self.prebuilt_path)
            return False
        has_changes = self.has_changes()
        if changes or has_changes \
            or self.has_missing_targets(envcopy):
            changes = True
            if has_changes and self.out_of_src_build:
                os.system("rm -rf %s"%self.build_path)
                os.mkdir(self.build_path)
            if not RUNNER.run_commands(self.build_commands,
                                       subdir=self.build_path):
                raise BuildFailure(self.name)

        if self.has_missing_targets(envcopy):
            raise MissingTargets(self.name)
        self.create_links(self.component_prefix)
        new_crc = self.calculate_crc()
        with open(self.crc_file, 'w') as crcfile:
            crcfile.write(new_crc)
        return changes

__all__ = ["GitRepoRetriever", "WebRetriever",
           "PreReqComponent", "NotInitialized",
           "DownloadFailure", "ExtractionError",
           "UnsupportedCompression", "BadScript",
           "MissingPath", "BuildFailure",
           "MissingDefinition", "MissingTargets"
          ]

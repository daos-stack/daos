```
OVERVIEW

scons_local is a set of build tools and scripts used to support
building and configuration of third party, rapidly developing tools
for use by libraries using these components.

LICENSE

scons_local is open source software distributed under the MIT License
Please see the [LICENSE](./LICENSE) & [NOTICE](./NOTICE) files for
more information.

RUNTIME OPTIONS OVERVIEW
Overview of runtime options added by prerequisite components

Option                             Description

--build-deps=<yes|no|build-only>   This controls if tarballs will be
                                   automatically downloaded and built.

--require-optional                 Tells the prerequisite builder
                                   to fail the build if calls to
                                   check_component will return False.

PREFIX=<path>                      The installation path of the user's
                                   components.   This should be used
                                   by ENV.Install as the location to
                                   install a scons-built component.
                                   A user can do this via:
                                   ENV.Alias('install', "$PREFIX")
                                   ENV.Install("$PREFIX/bin", program).
                                   By default, prerequisites will
                                   also be installed here.   If a
                                   user specifies TARGET_PREFIX,
                                   only symbolic links for such
                                   will be installed here.

TARGET_PREFIX=<path>               An alternative location to place
                                   prerequisite builds.  If set,
                                   Each individual prerequisite
                                   component will be installed in
                                   $TARGET_PREFIX/<component> and
                                   a symbolic link to the
                                   installation targets will be
                                   placed in $PREFIX.
                                   will be installed here.

PREBUILT_PREFIX=<path>[:<path>...] If set, the directories will be
                                   searched for component builds.
                                   Each component will be found
                                   in $PREBUILT_PREFIX/<component>.
                                   If a component is missing, it
                                   will be fetched and built
                                   using the definition of the
                                   component.

<COMPONENT>_PREBUILT=<path>        If set, the directory will be
                                   used as the already installed
                                   location for the component.
                                   If the location doesn't exist,
                                   the build will fail with an
                                   error.

SRC_PREFIX=<path>[:<path>...]      If set, the directories will be
                                   searched for component sources.
                                   Each component will be found
                                   in $SRC_PREFIX/<component>.
                                   If a component is missing, it
                                   will be fetched and built
                                   using the definition of the
                                   component.

<COMPONENT>_SRC=<path>             If set, the directory will be
                                   used as the already installed
                                   location for the component
                                   source.  If the location
                                   doesn't exist, the build will
                                   fail with an error.

USE_INSTALLED=<c1>[,<c2>[,...]]    When --build-deps is set, the
                                   default behavior is to build
                                   prerequisites.   Sometimes, we
                                   want to use an already installed
                                   version instead.  This allows
                                   one to specify a comma separated
                                   list of already installed
                                   components.  For all components,
                                   use 'all'.  Default is 'none'

ENV_SCRIPT=path>                   By default, the scripts will read
                                   $HOME/.scons_localrc as a SConscript
                                   giving the user the opportunity to
                                   modify the default environment used
                                   by the daos build.  This option
                                   allows specification of an alternate
                                   location.

GO_BIN=<path>                      Alternative path to go binary

COMPILER=<compiler>                Specify an alternate compiler.
                                   Supported options are icc, gcc,
                                   and clang.

BUILD_DIR=<path>                   Alternative path to place
                                   intermediate build targets.  Default
                                   is /path/to/daos_src/build

EXCLUDE=<component>                Components that should not be built.
                                   Only option is psm2 at present.

UNIT TESTING OVERVIEW
Overview of unit testing capabilities added by prereq_tools
When using prereq_tools, 3 new builders get added to your default environment.
RunTests - Takes a list of scons-built programs and runs them flagging a
           failure if any of them return a non-zero exit code
RunMemcheckTests - Takes a list of scons-built programs and runs them with
                   Valgrind memcheck, flagging a failure if any of them return
                   a non-zero exit code or if any errors are detected.
RunHelgrindTests - Takes a list of scons-built programs and runs them with
                   Valgrind helgrind, flagging a failure if any of them return
                   a non-zero exit code or if any errors are detected.

Example:
TESTS = ENV.Program("test1.c") + ENV.Program("test2.c")
RUN_TESTS = ENV.RunTests(TESTS)
# If the directory (e.g. utest) is specified on the command line,
# this will force it to actually run the tests even if the files
# have not changed.
AlwaysBuild(RUN_TESTS)

Jenkins:
If configuring Jenkins to find memcheck results, add **/memcheck-*.xml to your
Jenkins configuration.
```

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

BUILD_ROOT=<path>                  Alternative path to place
                                   intermediate build targets.  Default
                                   is /path/to/daos_src/build

BUILD_TYPE=dev|release|debug       Specify type of the build.

TARGET_TYPE=default|dev|release|   Specify type of prerequisite build.
            debug                  By default, prerequisites are
                                   installed in
                                   $PREFIX/prereq/$TARGET_TYPE/[component].
                                   By default, TARGET_TYPE is same as
                                   BUILD_TYPE.  If this is set, user can
                                   mix and match build types for daos
                                   vs prerequisites.

EXCLUDE=<component>                Components that should not be built.
                                   Only option is psm2 at present.

ALT_PREFIX=<path>[:<path2>...]     Prefix paths to search for already
                                   installed components.


```

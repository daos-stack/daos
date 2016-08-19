#!/bin/sh

function usage()
{
  cat << EOF
Usage: $0 [-c <config_file>] [<custom_build_script>]
       <config_file>          A file containing build configuration.
                              Default is build.config (See CONFIG)
       <custom_build_script>> An optional script sourced after a successful
                              build to run custom testing on the build
                              (See CUSTOM)

OVERVIEW
       This script is used to build components that use scons_local.  It
       is intended to be used on systems that have access to CORAL_ARTIFACTS
       as produced by Jenkins.   It utilizes the <name>-update-scratch jobs
       to grab required dependences for a component.  Therefore, a
       configuration file is required to specify the <name> of the component
       and its dependences.

CONFIG
       A build configuration file is required.  The file must have the
       the following line:

           component=<component>

       This specifies the name of the component being built.   There must be
       a corresponding <component>-update-scratch job

       It must also specify each dependence using the following format:

           depends=<name>[:<min_build>][:<parent_name>]

       Only dependences that have corresponding <name>-update-scratch jobs
       are specified.   For example, ompi-update-scratch also builds pmix
       and hwloc but only ompi should be specified as a dependence.
       The <min_build> specifies a Jenkins build number that is required.
       This is only used when the component update-scratch job is failing
       due to a breaking change in a dependence.  The <parent_name> defines
       a higher level component from which a default build nubmer can be
       pulled.   For example, one may specify iof as a parent for mcl,
       mercury, and ompi.   This is a way of telling the build script to
       use a dependent job to test lower level components to avoid
       broken versions.

DETAILS
       This tool uses the .build_info.sh script that is saved with various
       update-scratch jobs to determine default build numbers for dependences.
       It also uses Jenkins environment variables and configuration options
       to refine the decisions on versions to use.  It uses these values
       to set PREBUILT_PREFIX and PREFIX on the scons command line. By
       default, it only calls scons and scons install.   The user can add
       custom build steps to further test the build (See CUSTOM).

       For update-scratch builds, it automatically updates the latest link if
       the build and any user custom script runs without errors.

       There are 2 types of builds supported by this script:
           1. update-scratch This is a build that takes place after changes to
                             master or to dependent components.   The default
                             build numbers for each component come from the
                             latest good <name>-update-scratch or, if
                             applicable, <parent_name>-update-scratch for each
                             dependence.
           2. review         This is a build that takes place for reviews or
                             interactive builds.  In this case, the default
                             build number for each dependence is pulled from
                             the latest good <component>-update-scratch
                             build.

       If the default build number for a dependence needs to be updated (due
       to an API change, for example), then the user can specify <build_number>
       for that component to a value larger than the default.   The script
       will always choose the larger of the two values.

CUSTOM
       If a custom script is specified on the command line, the script will be
       sourced, if and only if the the build and install steps are successful.
       Before sourcing the script, COMP_PREFIX is set to the installation
       directory of the component.

EXAMPLE
       Building IOF, mcl, DAOS, and CPPR from the job script:
       This script normalizes the build between various components.  It is no
       longer necessary to copy boiler plate code into all of the components.
       This should make things more maintainable.

       The Jenkins job can be updated for each component to something like this

           cd <comp>
           scons_local/comp_build.sh comp_test.sh

       As an example, let's use CPPR
       In the case of CPPR, let's say it depends on at least iof build 33, mcl
       build 27, ompi build 88, and mercury build 123, we would create a config
       file, call it build.config,  with the following:

           component=cppr
           depends=iof:33
           depends=mcl:27:iof
           depends=ompi:88:iof
           depends=mercury:123:iof

       we would then create comp_test.sh with the following lines:

           scons utest
           scons utest --utest-mode=memcheck
           #Helgrind appears to be unstable
           scons utest --utest-mode=helgrind

           ${COMP_PREFIX}/TESTING/scripts/run_test.sh -l 4
           #Run memcheck test
           ${COMP_PREFIX}/TESTING/scripts/run_test.sh -e m -l 4

       And we would run with the command listed above.

       Notice the <parent_name> for mcl, ompi, and mercury is set to iof.
       This is for the cppr-update-scratch job to tell it to only use these
       components that have been blessed by a successful iof-update-scratch
       job.   For review or interactive building, it will always default to
       the version blessed by the latest cppr-update-scratch job.  In both
       cases, the <build_number> can force a newer version of a component.
EOF
  exit 1
}

function print_status()
{
    echo "*******************************************************************"
    echo $*
    echo "*******************************************************************"
}

function setup_dep()
{
  temp_ifs=$IFS
  IFS=':'
  array=($1)
  IFS=$TEMP_IFS

  len=${#array[@]}
  if [ $len -lt 1 ] || [ $len -gt 3 ]; then
    print_status "Bad argument $1"
    usage
  fi

  name=${array[0]}
  upper_name=${name^^}
  version=0
  cname=
  item=1
  while [ $item -lt $len ]; do
    if [[ ${array[$item]} = *[[:digit:]]* ]]; then
      version=${array[$item]}
    else
     cname=${array[$item]}
    fi
    item=$[ $item + 1 ]
  done

  echo "Setting up $upper_name.  Minimum version is $version."
  if [ -n "$cname" ]; then
    echo "Good version can be pulled from $cname"
  fi

  #Use the latest by default
  declare $upper_name=${CORAL_ARTIFACTS}/${name}-update-scratch/latest
  latest=
  if [ "${JOB_NAME}" != "${B_COMP}-update-scratch" ]; then
    #If it's not the scratch job, use a previous build of the scratch job
    echo "Getting $name build from $B_COMP latest"
    comp_name=${B_COMP}
    latest=$(readlink -f ${CORAL_ARTIFACTS}/${B_COMP}-update-scratch/latest)
  elif [ -n "$cname" ]; then
    #If it is the scratch job, use a child job, if specified
    echo "Getting $name build from $cname latest"
    latest=$(readlink -f ${CORAL_ARTIFACTS}/${cname}-update-scratch/latest)
    comp_name=${cname}
  fi
  if [ -n "$latest" ]; then
    for subdir in /${comp_name}/TESTING/ /TESTING/scripts/ /${comp_name}/ /; do
      vars=${latest}${subdir}.build_vars.sh
      if [ -f ${vars} ]; then
        break
      fi
    done
    if [ ! -f ${vars} ]; then
      print_status "$vars does not exist.   Check <build_vars> option"
      usage
    fi
    #These can be used to override the "last good version"
    #when there are breaking changes.   Jenkins will use
    #$GOOD_* only if it is newer than the last version
    #used by master
    good_varname=GOOD_${upper_name}
    good_version=${CORAL_ARTIFACTS}/${name}-update-scratch/$version
    declare ${good_varname}=${good_version}
    source ${vars}
    blessed_varname=SL_${upper_name}_PREFIX
    blessed_num=$(basename $(dirname ${!blessed_varname}))
    if [ $version -gt $blessed_num ]; then
      declare $upper_name=${!good_varname}
    else
      declare $upper_name=$(dirname ${!blessed_varname})
    fi
  fi
  PREBUILT_AREA=${PREBUILT_AREA}${!upper_name}:

  echo "$name version is ${!upper_name}"
}

BUILD_CONFIG=`pwd`/build.config
CUSTOM_SCRIPT=

while [ -n "$*" ]; do
  case "$1" in
    -c)
      shift
      BUILD_CONFIG=$(realpath $1)
      shift
      ;;
    *)
      CUSTOM_SCRIPT=$(realpath $1)
      shift
      ;;
  esac
done

if [ ! -f $BUILD_CONFIG ]; then
  print_status "$BUILD_CONFIG was not found"
  usage
fi

if [ -n "$CUSTOM_SCRIPT" ] && [ ! -f $CUSTOM_SCRIPT ]; then
  print_status "<custom_build_script> $CUSTOM_SCRIPT was not found"
  usage
fi

B_COMP=`cat $BUILD_CONFIG | grep "^component=" | sed 's/component=//'`
DEPS=`cat $BUILD_CONFIG | grep "^depends=" | sed 's/depends=//'`

if [ -z "$B_COMP" ]; then
  print_status "component must be specified in $BUILD_CONFIG"
  usage
fi

if [ -z "$WORKSPACE" ]; then
  CORAL_ARTIFACTS="/scratch/coral/artifacts"
  SET_PREFIX=
  B_INS_PATH=`pwd`/install/`uname -s`
  #Set job name so it thinks it's a review job
  JOB_NAME=invalid_job_name
  print_status "Not in Jenkins workspace"
else
  B_LINK_PATH="${CORAL_ARTIFACTS}/${JOB_NAME}/${BUILD_NUMBER}"
  B_INS_PATH="${B_LINK_PATH}/${B_COMP}"
  SET_PREFIX="PREFIX=${B_INS_PATH}"
  print_status "Building $B_INS_PATH in Jenkins workspace"
fi

if [ -d "$CORAL_ARTIFACTS" ]; then
  PREBUILT_AREA=
  for dep in $DEPS; do
    setup_dep $dep
  done
  option="PREBUILT_PREFIX=${PREBUILT_AREA} $SET_PREFIX"
fi

set -x
set -e
scons -c
rm -f ${B_COMP}-`uname -s`.conf
scons $option
scons install

if [ -n "$CUSTOM_SCRIPT" ]; then
  #custom build step
  print_status "Running custom build step"
  COMP_PREFIX=${B_INS_PATH}
  source $CUSTOM_SCRIPT
fi

if [ -n "${B_LINK_PATH}" ]; then
  ln -sfn ${BUILD_NUMBER} ${CORAL_ARTIFACTS}/${JOB_NAME}/latest
fi

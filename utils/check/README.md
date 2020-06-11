# Check Utilities

This directory has utilities to aid in doing some checks on the code
in or produced by this package.

The scripts in this directory may need to be modified to fit your build
environment.

## Scanning for CVEs for packages that DAOS depends on

The three scripts in this section can be used to make a list of packages
that the libraries installed by DAOS depend on, and then check the if
RedHat has any CVEs listed for them.
It will also report if if any of the binaries installed by DAOS
were built with out stack protection.

To use these scripts the following should be set up.

1. A test system with CentOS 7 installed including the `sudo` command.
1. These three scripts need to be copied to that system.
1. Recommend that you set up a working directory, as these scripts
   will put files and directories in it as they run.

### download_rpms.sh

This is the first script to run.

This is a script used for downloading RPMs for testing from a local
Jenkins system.

This script takes 2 optional parameters.

1. The branch name to download from.  Default is master.
   You can also specify just the release number for release branches.
1. The build number to download.  Default is the last successful build.

### handle_rpms.sh

This is the second script to run.

This is a script that will do the following.

1. Install the DAOS rpms that were previously downloaded.
1. Find all the ELF executables and shared libraries that were installed.
1. Run the hardening-check utility on all the ELF executables to check
   if stack protection is on.  It will produce a file with results.
1. Find all the shared libraries from other packages that DAOS depends on.
1. Find all the packages that provide those shared libraries.

### scan_depends.sh

This is the third script to run.

This script will use a file created by the first script to list any CVEs
that Red Hat knows about for the packages that DAOS depends on.

### Files created this script

The following environment variables are used to set the filenames and
directories used by these scripts.  There are default values used if the
environment variables are not set.

#### WORKSPACE

Defaults to the current working directory.

#### RPM_DIR

Defaults to "$WORKSPACE/rpm_dir".

#### SOFILES

Defaults to "daos_depends_libraries".  This is a list of libraries that
the downloaded RPMs depend on.

#### PACKAGES

Defaults to "daos_depends_packages".  This is a list of packages that
the downloaded RPMs depend on.

#### CHECKED_FILES

Defaults to "daos_checked_files".  This is a listing of any issues found by
the hardening-check program on the binaries in the downloaded RPMs.

#### CVE_LIST

Defaults to "daos_depends_cve_list".  This is a listing of any CVEs known
to RedHat for the packages that DAOS depends on.

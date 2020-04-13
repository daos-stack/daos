# Check Utilities

This directory has utilities to aid in doing some checks on the code
in or produced by this package.

The scripts in this directory may need to be modified to fit your build
environment.

## Scanning for CVEs for packages that DAOS depends on

The three scripts in this section can be used to make a list of packages
that the libraries installed by DAOS depend on, and then check the if
RedHat has any CVEs listed for them.

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
1. Find all the ELF executables and shared libaries that were installed.
1. Find all the shared libraries from other packages that DAOS depends on.
1. Find all the packages that provide those shared libraries.

### scan_depends.sh

This is the third script to run.

This script will use a file created by the first script to list any CVEs
that Red Hat knows about for the packages that DAOS depends on.

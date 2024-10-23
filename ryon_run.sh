# Built docker image
docker build --file ~/workspace/daos/utils/rpms/packaging/Dockerfile.mockbuild --tag mock-build  ~/workspace/daos/utils/rpms

# Trying to run
# LABEL: ci_vm9
# ARTIFACTS_URL: file:///scratch/job_repos/
#
# REPO_FILE_URL: https://artifactory.dc.hpdd.intel.com/artifactory/repo-files/
export BUILD_CHROOT=/var/lib/mock/rocky+epel-8-x86_64-10390383274/
export DISTRO_VERSION="8.8"
export STAGE_NAME="Functional on EL 8.8"
docker run --name mock-build-ryon --user build --privileged=true -e DAOS_FULLNAME="daos" -e DAOS_EMAIL="$DAOS_EMAIL" -e DISTRO_VERSION="$DISTRO_VERSION" -e STAGE_NAME="$STAGE_NAME" -e CHROOT_NAME="$CHROOT_NAME" -e ARTIFACTORY_URL="$ARTIFACTORY_URL" -e REPO_FILE_URL="$REPO_FILE_URL" -e JENKINS_URL="$JENKINS_URL" -e TARGET="$TARGET" -v $PWD:$PWD  mock-build ~/workspace/daos/ci/rpm/build.sh
#!/bin/bash

set -uex

: ${ARTIFACT_NEEDED:="lib/lib*"}

# Now save the specific git commit information used.
artifact_dest="${PWD}/artifacts"
rm -rf ${artifact_dest}
mkdir -p ${artifact_dest}

set -x
  for repo in ${REPO_LIST} ; do
    if [ -e ${repo} ]; then
      pushd ${repo}
        git rev-parse HEAD > ${artifact_dest}/${repo}_git_commit
      popd
    else
      git rev-parse HEAD > ${artifact_dest}/${repo}_git_commit
    fi
done

# Move the built products to its destination
# This could include making a tarball to be imported in a downstream job

for result in ${TARGET_LIST}; do
  pushd ${WORK_TARGET}/${TARGET}/${BUILD_NUMBER}
    mv ${result} ${artifact_dest}
  popd
  pushd artifacts/${result}
    tar -czf ../${result}_files.tar.gz .
  popd

  # Quick test to make sure something got built.
  ls artifacts/${result}/${ARTIFACT_NEEDED}
done


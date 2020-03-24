#!/bin/bash

set -uex

: "${ARTIFACT_NEEDED:="lib/lib*"}"

# Now save the specific git commit information used.
artifact_dest="${PWD}/artifacts/"
rm -rf "${artifact_dest}"
mkdir -p "${artifact_dest}"

set -x
  for repo in ${REPO_LIST} ; do
    if [ -e "${repo}" ]; then
      pushd "${repo}"
        git rev-parse HEAD > "${artifact_dest}${repo}_git_commit"
        #
        # There may or may not be a tag matching this commit.
        # If there is a tag, we want to use it.
        # As there are two types of tags, there are two ways to look
        # them up.  We will use the first tag we find.
        # Check for an exact match tag first
        tag_file="${artifact_dest}/${TARGET}_git_tag"
        if ! git describe --exact-match HEAD > "${tag_file}"; then
          rm "${artifact_dest}/${TARGET}_git_tag"
          # Look for a simple tag
          if ! git describe --contains HEAD > "${tag_file}"; then
            rm "${artifact_dest}/${TARGET}_git_tag"
          fi
        fi
      popd
    else
      git rev-parse HEAD > "${artifact_dest}${repo}_git_commit"
    fi
done

# Move the built products to its destination
# This could include making a tarball to be imported in a downstream job

for result in ${TARGET_LIST}; do
  pushd "${WORK_TARGET}"
    mv "${result}" "${artifact_dest}"
  popd
  pushd "artifacts/${result}"
    tar -czf "../${result}_files.tar.gz" .
  popd

  # Quick test to make sure something got built.
  # shellcheck disable=SC2086
  ls "artifacts/${result}/"${ARTIFACT_NEEDED}
done

# Find any build logs
find "${WORKSPACE}/${TARGET}" -name '*.log' -size -500k \
  -exec cp {} "${artifact_dest}" \;
find "${WORKSPACE}/${TARGET}" -name '*.log' -size +500k \
  -exec printf "%s\n" \; >> "${artifact_dest}/log_files_over_500k.log"


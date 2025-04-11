#!/bin/bash
#
# Usage: archive.sh <dir> <name> <version> <extension>
#
# Create an archive of the HEAD commit of a local Git repository, including
# submodules.
#
#   - Must run at the root of the Git repository.
#   - Must have all submodules recursively initialized and updated.
#   - Whitespaces are prohibited in any of the parameters.
#
# For example,
#
#   archive.sh /a/b/c daos 1.2.3 tar
#
# produces /a/b/c/daos-1.2.3.tar.
#

set -e

dir=$1
name=$2
version=$3
ext=$4

# Use a temporary directory for all intermediate files.
unset tmp
trap 'if [ -n "${tmp}" ]; then rm -rf "${tmp}"; fi' EXIT
tmp=$(mktemp -d)

file_extless="${name}-${version}"
file="${file_extless}.${ext}"
sm_file_prefix="${file_extless}-submodule"

# Create an archive, which doesn't include any submodule.
git archive --prefix "${name}-${version}/" -o "${tmp}/${file}" HEAD

# Add all submodules to the archive.
# shellcheck disable=SC2086,SC2016
git submodule --quiet foreach --recursive \
    'tarfile='${tmp}/${sm_file_prefix}'-${name//\//}.'${ext}' && \
     git archive --prefix '${name}-${version}'/$displaypath/ -o ${tarfile} $sha1 && \
     tar -Af '"${tmp}/${file}"' ${tarfile}'

# Publish the archive.
mv "${tmp}/${file}" "${dir}/${file}"

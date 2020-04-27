#!/bin/bash -l

# Only need to do any of this if the version has been updated
# NOTE: The diff-index with HEAD^ implies that the TAG
#       must be updated in the last commit.  But version update
#       PRs really should just be a single commit since we
#       script the creation of the version update PR.
if ! git diff-index --name-only HEAD^ | grep -q TAG; then
     echo "TAG not updated, exiting"
     exit 0
fi

# Don't create a release for the first time the TAG file is
# added as that point is most surely not an actual release.
if git diff-tree --diff-filter=A --no-commit-id --name-status -r HEAD |
   grep -q TAG; then
     echo "TAG being added not updated, exiting"
     exit 0
fi

release=$(cat TAG)

if [[ $release = *-* ]]; then
    prerelease="true"
else
    prerelease="false"
fi
# Ensure that the GITHUB_TOKEN secret is included
if [ -z "$GITHUB_TOKEN" ]; then
  echo "Set the GITHUB_TOKEN env variable."
  exit 1
fi

json="{
  \"tag_name\": \"v$release\",
  \"target_commitish\": \"$GITHUB_SHA\",
  \"name\": \"Release $release\",
  \"body\": \"DAOS release $release\",
  \"draft\": false,
  \"prerelease\": $prerelease
}"

curl --request POST \
  --url https://api.github.com/repos/"${GITHUB_REPOSITORY}"/releases \
  --header "Authorization: Bearer $GITHUB_TOKEN" \
  --header 'Content-Type: application/json' \
  --data "$json"

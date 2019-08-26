#!/bin/sh -l

request_create_release(){

	local json_body='{
	  "tag_name": "@tag_name@",
	  "target_commitish": "@branch@",
	  "name": "@release_name@",
	  "body": "@description@",
	  "draft": false,
	  "prerelease": false
	}'

	json_body=$(echo "$json_body" | sed "s/@tag_name@/$git_tag/")
	json_body=$(echo "$json_body" | sed "s/@branch@/master/")
	json_body=$(echo "$json_body" | sed "s/@release_name@/Release $git_tag/")
	json_body=$(echo "$json_body" | sed "s/@description@/$DESCRIPTION/")

	curl --request POST \
	  --url https://api.github.com/repos/${GITHUB_REPOSITORY}/releases \
	  --header "Authorization: Bearer $GITHUB_TOKEN" \
	  --header 'Content-Type: application/json' \
	  --data "$json_body"
}

if tag=$(git log --format=%B -n 1 | sed -n '/^[Rr]elease:/s/^[Rr]elease: *//p'); then
    echo "Release requested is: $tag"
else
    echo "No release request"
fi

echo "Environment:"
env

# Ensure that the GITHUB_TOKEN secret is included
if [[ -z "$GITHUB_TOKEN" ]]; then
  echo "Set the GITHUB_TOKEN env variable."
  exit 1
fi

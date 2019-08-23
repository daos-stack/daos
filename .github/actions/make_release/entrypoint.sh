#!/bin/sh -l

if tag=$(git log --format=%B -n 1 | sed -n '/^[Tt]ag:/s/^[Tt]ag: *//p'); then
    echo "Tag requested is: $tag"
else
    echo "No tag request"
fi

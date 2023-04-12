#!/bin/bash

VERSION=$(git --version | sed -ne 's/^[^0-9]*//p' | cut -d ' ' -f 1)
if [ -z "$VERSION" ]; then
    echo "  ERROR: Could not determine git version."
    exit 1
fi

git_vercode=0
mult=1000000
IFS='.' read -ra vs <<< "$VERSION"
for i in "${vs[@]}"; do
    git_vercode=$((git_vercode + i * mult))
    mult=$((mult / 1000))
done

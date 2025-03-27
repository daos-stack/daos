#!/bin/sh

set -e
set -v

if test -z "$COVERITY_TOKEN"
then
    echo "COVERITY_TOKEN environment variable must be set"
    exit 1
fi

if test -z "$COVERITY_EMAIL"
then
    echo "COVERITY_EMAIL environment variable must be set"
    exit 1
fi

GIT_SHA=$(git rev-parse --short HEAD)

curl -sS -L -o coverity.tar.gz \
     -d "token=$COVERITY_TOKEN&project=nutanix%2Flibvfio-user" \
     https://scan.coverity.com/download/cxx/linux64

tar xf coverity.tar.gz

meson build/coverity || (cat build/meson-logs/meson-log.txt && exit 1)
./cov-analysis-linux64-*/bin/cov-build --dir cov-int ninja -C build/coverity -v

tar czf coverity-results.tar.gz cov-int

curl --form token=$COVERITY_TOKEN \
     --form email=$COVERITY_EMAIL \
     --form file=@coverity-results.tar.gz \
     --form version=$GIT_SHA \
     https://scan.coverity.com/builds?project=nutanix%2Flibvfio-user

#!/bin/bash
## Run linters across control plane code and execute Go tests
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

oldGP=$GOPATH
export GOPATH=$DIR/../../build/src/control
echo "GOPATH $GOPATH"

# Lint source then run Go tests for each package
for d in mgmt security server dmg; do
    echo "testing $d"
    pushd "$DIR/$d"
    # todo: provide a sensible way of linting and returning review comments
    # gofmt -l -s -w . && go tool vet -all . && golint && goimports -w .
    go test -v
    popd
done

export GOPATH=$oldGP

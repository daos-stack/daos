#!/bin/bash
## Run linters across control plane code and execute Go tests
set -e

DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null && pwd )"

oldGP=$GOPATH
export GOPATH=$DIR/../../build/src/control
echo "GOPATH $GOPATH"

repopath=github.com/daos-stack/daos

# Lint source then run Go tests for each package
for d in client security server dmg drpc; do
    echo "testing $d"
    pushd "$GOPATH/src/$repopath/src/control/$d"
    # todo: provide a sensible way of linting and returning review comments
    # gofmt -l -s -w . && go tool vet -all . && golint && goimports -w .
    go test -v
    popd
done

export GOPATH=$oldGP

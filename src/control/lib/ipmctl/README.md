# Go language bindings for the ipmctl API for managing PMMs

Go bindings for the libipmctl native C lib to enable management of DCPMMs from Go source 

This is a Go interface for [ipmctl](https://github.com/intel/ipmctl)

## How to Build

This is a [Go](https://golang.orghttps://golang.org/doc/install)
project, so the Go development tools are naturally required. We
recommend the most current Go release available. As of July 2018, the project has been built and tested with Go 1.11.5

The following steps assume ipmctl library is installed. To install please follow steps in the [ipmctl github](https://github.com/intel/ipmctl)

Clone the repo:

    git clone git@github.com:daos-stack/go-ipmctl.git
    export GOIPMCTL=${HOME}/go-ipmctl

Setup your environment:

    export GOPATH=${HOME}/go
    export PATH=${PATH}:${GOPATH}/bin/

Install go-task:

    go get -u -v github.com/go-task/task/cmd/task

Install [golint](https://github.com/golang/lint):

    go get -u -v golang.org/x/lint/golint

Install [goimport](https://godoc.org/golang.org/x/tools/cmd/goimports):

    go get -u -v golang.org/x/tools/cmd/goimports

### Build with Taskfile

Run task:

    task main-task

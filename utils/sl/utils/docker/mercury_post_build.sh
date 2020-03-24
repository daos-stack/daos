#!/bin/bash

mchecksum_commit="${PWD}/artifacts/mercury_mchecksum_git_commit"
kwsys_commit="${PWD}/artifacts/mercury_kwsys_git_commit"

pushd mercury
  pushd src/mchecksum
    git rev-parse HEAD > "${mchecksum_commit}"
  popd
  pushd Testing/driver/kwsys
    git rev-parse HEAD > "${kwsys_commit}"
  popd
popd


#!/bin/bash

. ci/functions.sh

nodes=${nodes:-"vm[1-8]"}

rpc "" "$nodes" test_rpc1

rpc "" "$nodes" test_rpc2 "bar"

rpc "" "$nodes" test_rpc3 "bar" "bat"

rpc "" "$nodes" test_rpc3 "bar bie" "bat"
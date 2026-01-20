#!/bin/bash

set -uex

git clean -dxf

bandit --format xml -o bandit.xml -r . -c ci/bandit.config || true

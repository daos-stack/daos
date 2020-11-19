#!/bin/bash

set -uex

gitfiles=(.)
if [ -e '.git' ]; then
  gitfiles=($(git ls-tree --name-only HEAD))
fi

bandit --format xml -o bandit.xml -r "${gitfiles[@]}" \
       -c src/tests/ftest/security/bandit.config || true

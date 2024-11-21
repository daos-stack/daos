#!/bin/bash

set -eo pipefail
trap 'echo "An unexpected error occurred. Exiting."' ERR

if [[ ! -d /opt/fio ]]; then
  pushd /opt
  git clone https://git.kernel.org/pub/scm/linux/kernel/git/axboe/fio.git
  cd fio
  ./configure
  sudo make install
  popd
fi

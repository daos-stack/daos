#!/bin/bash

set -eux

clush -B -l root -w "$NODESTRING" --connect_timeout 30 \
      -S "$(cat ci/provisioning/log_cleanup_nodes_.sh)"

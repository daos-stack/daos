#!/bin/bash

# set -x
set -e -o pipefail

CWD="$(realpath "$(dirname $0)")"

NODESET=${1:?Missing nodeset parameter}
USERNAME=${2:?Missing user name}

echo "[INFO] Setup sudoers for user $USERNAME on nodeset $NODESET"
echo "$USERNAME ALL=(ALL) NOPASSWD: ALL" | clush -BLS -w $NODESET -l root "bash -c 'cat > /etc/sudoers.d/$USERNAME' ; chmod 600 /etc/sudoers.d/$USERNAME"

#!/bin/bash

# set -x
set -u -e -o pipefail

CWD="$(realpath "$(dirname "$0")")"

source "$CWD/env.sh"

set +e
{
	cat <<- EOF
	# set -x
	set -e -o pipefail

	pushd "$DAOS_SRC" > /dev/null
	source utils/sl/setup_local.sh &> /dev/null
	popd > /dev/null
	set -u

	echo
	echo ">>> Stopping DAOS"
	$DMG_BIN $DMG_OPTS system stop
	EOF
} | ssh root@$ADMIN_NODE bash -s

echo
echo ">>> Stopping DAOS services"
clush -BLS -w $CLIENT_NODES -l root systemctl stop daos_agent
clush -BLS -w $SERVER_NODES -l root systemctl stop daos_server

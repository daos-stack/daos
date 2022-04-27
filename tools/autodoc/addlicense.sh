#!/bin/bash
# Copyright 2022 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"
ADDLICENSE_VERSION="1.0.0"
ADDLICENSE_TAG_URL="https://github.com/google/addlicense/releases/download/v${ADDLICENSE_VERSION}"

ADDLICENSE_COMPANY_NAME="${ADDLICENSE_COMPANY_NAME:-Intel Corporation}"

pushd () {
    command pushd "$@" > /dev/null
}

popd () {
    command popd "$@" > /dev/null
}

download_addlicense() {
	case $(uname | tr '[:upper:]' '[:lower:]') in
		linux*)
		  ADDLICENSE_ARCHIVE="addlicense_${ADDLICENSE_VERSION}_Linux_${HOSTTYPE}.tar.gz"
			;;
		darwin*)
			ADDLICENSE_ARCHIVE="addlicense_${ADDLICENSE_VERSION}_macOS_${HOSTTYPE}.tar.gz"
			;;
		*)
			echo "Unsupported OS!"
			exit 1
	esac
	echo "Downloading ${ADDLICENSE_TAG_URL}/${ADDLICENSE_ARCHIVE}"
	curl  -s -L -O "${ADDLICENSE_TAG_URL}/${ADDLICENSE_ARCHIVE}"
	if [ -f "${SCRIPT_DIR}/${ADDLICENSE_ARCHIVE}" ]; then
		tar -xz --file "${ADDLICENSE_ARCHIVE}" addlicense
		chmod +x addlicense
		rm -f "${SCRIPT_DIR}/${ADDLICENSE_ARCHIVE}"
	fi
}

pushd .
cd "${SCRIPT_DIR}"

if [ "$(which addlicense)" ]; then
	ADDLICENSE_PATH="$(which addlicense)"
else
	if [ ! -f "${SCRIPT_DIR}/addlicense" ]; then
		echo "Need to download the 'addlicense' binary"
		download_addlicense
	fi
  ADDLICENSE_PATH="${SCRIPT_DIR}/addlicense"
fi

popd
${ADDLICENSE_PATH} -c "${ADDLICENSE_COMPANY_NAME}" -l apache "$@"

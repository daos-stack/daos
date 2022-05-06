
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

#
# Generate private CA, certs and keys.
# Add them to a GCP Secret Manager secret.
#
# This script only needs to be run once in a DAOS cluster.
# It is run from a startup script on the first DAOS server.
#
# In order for this script to add a Secret Version to a given secret, the
# service account that is running the instance must be given the proper
# permissions on the secret. Typically the secret is created by Terraform
# and therefore it is owned by the user who is running Terraform. The
# daos_server Terraform module will create the secret and apply the necessary
# policies.
#

set -ue
trap 'echo "An unexpected error occurred. Exiting."' ERR

SECRET_NAME="$1"
DAOS_DIR=/var/daos
CERT_GEN_SCRIPTS_DIR="${DAOS_DIR}/cert_gen"

if [[ -z "${SECRET_NAME}" ]]; then
  echo "ERROR: Secret name must be passed as the first parameter. Exiting..."
  exit 1
fi

if [[ ! -f "${CERT_GEN_SCRIPTS_DIR}/gen_certificates.sh" ]]; then
  echo "ERROR: File not found '${CERT_GEN_SCRIPTS_DIR}/gen_certificates.sh'"
  exit 1
fi

cd "${DAOS_DIR}"

# Generate the daosCA directory that contains the certs and keys
"${CERT_GEN_SCRIPTS_DIR}/gen_certificates.sh" "${DAOS_DIR}"
# Create archive of the daosCA directory
tar -cvzf "${DAOS_DIR}/daosCA.tar.gz" ./daosCA
# Store daosCA.tar.gz in Google Cloud Secret Manager
gcloud secrets versions add ${SECRET_NAME} --data-file="${DAOS_DIR}/daosCA.tar.gz"
# Delete certs archive now that it has been added to Secret Manager
rm -f "${DAOS_DIR}/daosCA.tar.gz"

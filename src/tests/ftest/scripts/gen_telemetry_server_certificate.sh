#!/bin/bash
# /*
#  * (C) Copyright 2024 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

__usage="

This is just an example script for testing purpose. 
Please modify to use in Production environment.

Usage: gen_telemetry_server_certificate.sh [USER] [DIR]
    USER: DAOS has server and client and the certificate need the specific file permission
          based on system usage.
          Use daos_server if running script on server
          Use daos_agent if running script on client

    DIR: Generate telemetry certificates for DAOS metrics in the [DIR].
         By default [DIR] is the current directory.
"
DAYS=1095

USER=$1
CA_HOME="${2:-.}/"
HOSTNAME=$(hostname -s)

openssl req -x509 -newkey rsa:4096 -keyout "${CA_HOME}/telemetry.key" -out "${CA_HOME}/telemetry.crt" -sha256 -days ${DAYS} -nodes -subj "/CN=\"${HOSTNAME}\""
chmod 0400 "${CA_HOME}/telemetry.key"
chmod 0644 "${CA_HOME}/telemetry.crt"
chown "${USER}"."${USER}" "${CA_HOME}/telemetry.key"
chown "${USER}"."${USER}" "${CA_HOME}/telemetry.crt"

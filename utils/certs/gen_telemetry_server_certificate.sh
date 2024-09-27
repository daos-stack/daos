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
          Use "daos_server" if running script on server
          Use "daos_agent" if running script on client

    DIR: Generate telemetry certificates for DAOS metrics in the [DIR].
         By default [DIR] is the current directory.
"

DAYS=1095

USER=$1
CA_HOME="${2:-.}/"
HOSTNAME=$(hostname -s)

function print_usage () {
    >&2 echo "$__usage"
}

function generate_ca_cnf () {
    echo "
[req]
default_md = sha256
prompt = no
req_extensions = v3_ext
distinguished_name = req_distinguished_name

[req_distinguished_name]
CN = ${HOSTNAME}

[v3_ext]
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = critical,serverAuth,clientAuth
subjectAltName = DNS:${HOSTNAME}

" > "${CA_HOME}/telemetry.cnf"
}

function generate_server_cert () {
    echo "Generating Server Certificate"
    # Generate Private key and set its permissions
    openssl genrsa -out "${CA_HOME}/telemetryserver.key" 2048
    [[ $EUID -eq 0 ]] && chown ${USER}.${USER} "${CA_HOME}/telemetryserver.key"
    chmod 0400 "${CA_HOME}/telemetryserver.key"

    # Generate a Certificate Signing Request (CRS)
    openssl req -new -key "${CA_HOME}/telemetryserver.key" \
        -out "${CA_HOME}/telemetryserver.csr" -config "${CA_HOME}/telemetry.cnf"

    # Create Certificate from request
    openssl x509 -req -in "${CA_HOME}/telemetryserver.csr" -CA "${CA_HOME}/daosTelemetryCA.crt" \
        -CAkey "${CA_HOME}/daosTelemetryCA.key" -CAcreateserial -out "${CA_HOME}/telemetryserver.crt" \
        -days ${DAYS} -sha256 -extfile "$CA_HOME/telemetry.cnf" -extensions v3_ext

    [[ $EUID -eq 0 ]] && chown ${USER}.${USER} "${CA_HOME}/telemetryserver.crt"
    chmod 0644 "${CA_HOME}/telemetryserver.crt"

    echo "Required Server Certificate Files:
    ${CA_HOME}/daosTelemetryCA.crt
    ${CA_HOME}/telemetryserver.key
    ${CA_HOME}/telemetryserver.crt"
}

function cleanup () {
    # Remove this key as it's not required after creating the telemetryserver.key
    rm -f "${CA_HOME}/daosTelemetryCA.key"

    rm -f "${CA_HOME}/telemetryserver.csr"
    rm -f "${CA_HOME}/telemetry.cnf"
}

function main () {
    generate_ca_cnf
    generate_server_cert
    cleanup
}

main

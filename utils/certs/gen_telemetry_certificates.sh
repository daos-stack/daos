#!/bin/bash
# /*
#  * (C) Copyright 2016-2024 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

__usage="

This is just an example script for testing purpose. 
Please modify to use in Production environment.

Usage: gen_telemetry_certificates.sh [DIR]
Generate telemetry certificates for DAOS metrics in the [DIR]/daosCA.
By default [DIR] is the current directory.
"

DAYS=1095

CA_HOME="${1:-.}/daosCA"
PRIVATE="${CA_HOME}/private"
CERTS="${CA_HOME}/certs"

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
    openssl genrsa -out "${CERTS}/telemetryserver.key" 2048
    [[ $EUID -eq 0 ]] && chown daos_server.daos_server "${CERTS}/telemetryserver.key"
    chmod 0400 "${CERTS}/telemetryserver.key"

    # Generate a Certificate Signing Request (CRS)
    openssl req -new -key "${CERTS}/telemetryserver.key" \
        -out "${CA_HOME}/telemetryserver.csr" -config "${CA_HOME}/telemetry.cnf"

    # Create Certificate from request
    openssl x509 -req -in "${CA_HOME}/telemetryserver.csr" -CA "${CERTS}/daosCA.crt" \
        -CAkey "${PRIVATE}/daosCA.key" -CAcreateserial -out "${CERTS}/telemetryserver.crt" \
        -days ${DAYS} -sha256 -extfile "$CA_HOME/telemetry.cnf" -extensions v3_ext

    [[ $EUID -eq 0 ]] && chown daos_server.daos_server "${CERTS}/telemetryserver.crt"
    chmod 0644 "${CERTS}/telemetryserver.crt"

    echo "Required Server Certificate Files:
    ${CERTS}/daosCA.crt
    ${CERTS}/telemetryserver.key
    ${CERTS}/telemetryserver.crt"
}

function cleanup () {
    rm -f "${CA_HOME}/telemetryserver.csr"
    rm -f "${CA_HOME}/telemetry.cnf"
}

function main () {
    generate_ca_cnf
    generate_server_cert
    cleanup
}

main

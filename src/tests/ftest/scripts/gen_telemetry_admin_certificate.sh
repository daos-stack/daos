#!/bin/bash
# /*
#  * (C) Copyright 2024 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

__usage="
Usage: gen_telemetry_admin_certificate.sh [DIR]
Generate certificates for DAOS deployment in the [DIR]/daosTelemetryCA.
By default [DIR] is the current directory.
"

function print_usage () {
    >&2 echo "$__usage"
}

# validity of root CA and keys' certificates
DAYS=1095

CA_HOME="${1:-.}/daosTelemetryCA"

function setup_directories () {
    mkdir -p "${CA_HOME}"
    chmod 700 "${CA_HOME}"
}

function generate_ca_cnf () {
    echo "
[ ca ]
default_ca              = CA_daos_telemetry

[ CA_daos_telemetry ]
dir                     = ${CA_HOME}
certs                   = \$dir

# Key and Certificate for the root
certificate             = \$dir/daosTelemetryCA.crt
private_key             = \$dir/daosTelemetryCA.key

default_md              = sha512        # SAFE Crypto Requires SHA-512
default_days            = ${DAYS}       # how long to certify for
copy_extensions         = copy
unique_subject          = no

[ req ]
prompt = no
distinguished_name = ca_dn
x509_extensions = ca_ext

[ ca_dn ]
organizationName        = DAOS
commonName              = DAOS CA TELEMETRY

[ ca_ext ]
keyUsage = critical,digitalSignature,nonRepudiation,keyEncipherment,keyCertSign
basicConstraints = critical,CA:true,pathlen:1

[ signing_policy ]
organizationName        = supplied
commonName              = supplied

" > "${CA_HOME}/ca.cnf"
}

function generate_ca_cert () {
    echo "Generating Private CA Root Certificate"
    # Generate Private key and set permissions
    openssl genrsa -out "${CA_HOME}/daosTelemetryCA.key" 3072
    [[ $EUID -eq 0 ]] && chown root.root "${CA_HOME}/daosTelemetryCA.key" 2>/dev/null
    chmod 0400 "${CA_HOME}/daosTelemetryCA.key"
    # Generate CA Certificate
    openssl req -new -x509 -config "${CA_HOME}/ca.cnf" -days ${DAYS} -sha512 \
        -key "${CA_HOME}/daosTelemetryCA.key" \
        -out "${CA_HOME}/daosTelemetryCA.crt" -batch
    [[ $EUID -eq 0 ]] && chown root.daos_daemons "${CA_HOME}/daosTelemetryCA.crt" 2>/dev/null
    chmod 0644 "${CA_HOME}/daosTelemetryCA.crt"
    # Reset the the CA index
    rm -f "${CA_HOME}/index.txt" "${CA_HOME}/serial.txt"
    touch "${CA_HOME}/index.txt"
    echo '01' > "${CA_HOME}/serial.txt"
    echo "Private CA Root Certificate for Telemetry created in ${CA_HOME}"
}

function cleanup () {
    rm -f "${CA_HOME}/ca.cnf"
}

function main () {
    if [[ -d "$CA_HOME" ]]
    then
      echo "$CA_HOME already exists, exiting."
      exit 1
    fi
    setup_directories
    generate_ca_cnf
    generate_ca_cert
    cleanup
}

main
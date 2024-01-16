#!/bin/bash
# /*
#  * (C) Copyright 2016-2022 Intel Corporation.
#  *
#  * SPDX-License-Identifier: BSD-2-Clause-Patent
# */

__usage="
Usage: gen_certificates.sh [DIR]
Generate certificates for DAOS deployment in the [DIR]/daosCA.
By default [DIR] is the current directory.
"

function print_usage () {
    >&2 echo "$__usage"
}

# validity of root CA and keys' certificates
DAYS=1095

CA_HOME="${1:-.}/daosCA"
PRIVATE="${CA_HOME}/private"
CERTS="${CA_HOME}/certs"
CLIENTS="${CERTS}/clients"
# shellcheck disable=SC2128
CONFIGS="$(dirname "${BASH_SOURCE}")"

function setup_directories () {
    mkdir -p "${CA_HOME}"
    chmod 700 "${CA_HOME}"
    mkdir -p "${PRIVATE}"
    chmod 700 "${PRIVATE}"
    mkdir -p "${CERTS}"
    chmod 755 "${CERTS}"
}

function generate_ca_cnf () {
    echo "
[ ca ]
default_ca              = CA_daos

[ CA_daos ]
dir                     = ${CA_HOME}
certs                   = \$dir/certs
database                = \$dir/index.txt
serial                  = \$dir/serial.txt

# Key and Certificate for the root
certificate             = \$dir/daosCA.crt
private_key             = \$dir/private/daosCA.key

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
commonName              = DAOS CA

[ ca_ext ]
keyUsage = critical,digitalSignature,nonRepudiation,keyEncipherment,keyCertSign
basicConstraints = critical,CA:true,pathlen:1

[ signing_policy ]
organizationName        = supplied
commonName              = supplied

[ signing_agent ]
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = clientAuth

[ signing_server ]
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = serverAuth, clientAuth

[ signing_admin ]
keyUsage = critical,digitalSignature,keyEncipherment
extendedKeyUsage = clientAuth

" > "${CA_HOME}/ca.cnf"
}

function generate_ca_cert () {
    echo "Generating Private CA Root Certificate"
    # Generate Private key and set permissions
    openssl genrsa -out "${PRIVATE}/daosCA.key" 3072
    [[ $EUID -eq 0 ]] && chown root.root "${PRIVATE}/daosCA.key" 2>/dev/null
    chmod 0400 "${PRIVATE}/daosCA.key"
    # Generate CA Certificate
    openssl req -new -x509 -config "${CA_HOME}/ca.cnf" -days ${DAYS} -sha512 \
        -key "${PRIVATE}/daosCA.key" \
        -out "${CERTS}/daosCA.crt" -batch
    [[ $EUID -eq 0 ]] && chown root.daos_daemons "${CERTS}/daosCA.crt" 2>/dev/null
    chmod 0644 "${CERTS}/daosCA.crt"
    # Reset the the CA index
    rm -f "${CA_HOME}/index.txt" "${CA_HOME}/serial.txt"
    touch "${CA_HOME}/index.txt"
    echo '01' > "${CA_HOME}/serial.txt"
    echo "Private CA Root Certificate created in ${CA_HOME}"
}

function generate_agent_cert () {
    echo "Generating Agent Certificate"
    # Generate Private key and set its permissions
    openssl genrsa -out "${CERTS}/agent.key" 3072
    [[ $EUID -eq 0 ]] \
        && chown daos_agent.daos_agent "${CERTS}/agent.key" 2>/dev/null
    chmod 0400 "${CERTS}/agent.key"
    # Generate a Certificate Signing Request (CRS)
    openssl req -new -config "${CONFIGS}/agent.cnf" -key "${CERTS}/agent.key" \
        -out "${CA_HOME}/agent.csr" -batch
    # Create Certificate from request
    openssl ca -config "${CA_HOME}/ca.cnf" -keyfile "${PRIVATE}/daosCA.key" \
        -cert "${CERTS}/daosCA.crt" -policy signing_policy \
        -extensions signing_agent -out "${CERTS}/agent.crt" \
        -outdir "${CERTS}" -in "${CA_HOME}/agent.csr" -batch
    [[ $EUID -eq 0 ]] \
        && chown daos_agent.daos_agent "${CERTS}/agent.crt" 2>/dev/null
    chmod 0644 "${CERTS}/agent.crt"

    echo "Required Agent Certificate Files:
    ${CERTS}/daosCA.crt
    ${CERTS}/agent.key
    ${CERTS}/agent.crt"
}

function generate_admin_cert () {
    echo "Generating Admin Certificate"
    # Generate Private key and set its permissions
    openssl genrsa -out "${CERTS}/admin.key" 3072
    # TODO [[ $EUID -eq 0 ]] && chown ?.? "${CERTS}/admin.key"
    chmod 0400 "${CERTS}/admin.key"
    # Generate a Certificate Signing Request (CRS)
    openssl req -new -config "${CONFIGS}/admin.cnf" -key "${CERTS}/admin.key" \
        -out "${CA_HOME}/admin.csr" -batch
    # Create Certificate from request
    openssl ca -config "${CA_HOME}/ca.cnf" -keyfile "${PRIVATE}/daosCA.key" \
        -cert "${CERTS}/daosCA.crt" -policy signing_policy \
        -extensions signing_admin -out "${CERTS}/admin.crt" \
        -outdir "${CERTS}" -in "${CA_HOME}/admin.csr" -batch
    # TODO [[ $EUID -eq 0 ]] && chown ?.? "${CERTS}/admin.crt"
    chmod 0644 "${CERTS}/admin.crt"

    echo "Required Admin Certificate Files:
    ${CERTS}/daosCA.crt
    ${CERTS}/admin.key
    ${CERTS}/admin.crt"
}

function generate_server_cert () {
    echo "Generating Server Certificate"
    # Generate Private key and set its permissions
    openssl genrsa -out "${CERTS}/server.key" 3072
    [[ $EUID -eq 0 ]] && chown daos_server.daos_server "${CERTS}/server.key"
    chmod 0400 "${CERTS}/server.key"
    # Generate a Certificate Signing Request (CRS)
    openssl req -new -config "${CONFIGS}/server.cnf" \
        -key "${CERTS}/server.key" -out "${CA_HOME}/server.csr" -batch
    # Create Certificate from request
    openssl ca -config "$CA_HOME/ca.cnf" -keyfile "${PRIVATE}/daosCA.key" \
        -cert "${CERTS}/daosCA.crt" -policy signing_policy \
        -extensions signing_server -out "${CERTS}/server.crt" \
        -outdir "${CERTS}" -in "${CA_HOME}/server.csr" -batch
    [[ $EUID -eq 0 ]] && chown daos_server.daos_server "${CERTS}/server.crt"
    chmod 0644 "${CERTS}/server.crt"

    echo "Required Server Certificate Files:
    ${CERTS}/daosCA.crt
    ${CERTS}/server.key
    ${CERTS}/server.crt"
}

function populate_clients_dir () {
    mkdir -p                      "${CLIENTS}"
    chmod 755                     "${CLIENTS}"
    chown daos_server.daos_server "${CLIENTS}"
    cp "${CERTS}/admin.crt"       "${CLIENTS}/admin.crt"
    chown daos_server.daos_server "${CLIENTS}/admin.crt"
    chmod 644                     "${CLIENTS}/admin.crt"
    cp "${CERTS}/agent.crt"       "${CLIENTS}/agent.crt"
    chown daos_server.daos_server "${CLIENTS}/agent.crt"
    chmod 644                     "${CLIENTS}/agent.crt"

    echo "Authorized Clients Certificate Files on DAOS Servers:
    ${CLIENTS}/agent.crt
    ${CLIENTS}/admin.crt"
}

function cleanup () {
    rm -f "${CERTS}/*.pem"
    rm -f "${CA_HOME}/agent.csr"
    rm -f "${CA_HOME}/admin.csr"
    rm -f "${CA_HOME}/server.csr"
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
    generate_server_cert
    generate_agent_cert
    generate_admin_cert
    populate_clients_dir
    cleanup
}

main

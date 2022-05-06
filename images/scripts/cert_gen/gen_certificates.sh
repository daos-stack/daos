#!/bin/bash
# Copyright 2019-2022 Intel Corporation.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted for any purpose (including commercial purposes)
# provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice,
#    this list of conditions, and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions, and the following disclaimer in the
#    documentation and/or materials provided with the distribution.
#
# 3. In addition, redistributions of modified forms of the source or binary
#    code must carry prominent notices stating that the original code was
#    changed and the date of the change.
#
#  4. All publications or advertising materials mentioning features or use of
#     this software are asked, but not required, to acknowledge that it was
#     developed by Intel Corporation and credit the contributors.
#
# 5. Neither the name of Intel Corporation, nor the name of any Contributor
#    may be used to endorse or promote products derived from this software
#    without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
# THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


__usage="
Usage: gen_certificates.sh [DIR]
Generate certificates for DAOS deployment in the [DIR]/daosCA.
By default [DIR] is the current directory.
"

function print_usage () {
    >&2 echo "$__usage"
}

CA_HOME="${1:-.}/daosCA"
PRIVATE="${CA_HOME}/private"
CERTS="${CA_HOME}/certs"
CONFIGS="$(dirname "${BASH_SOURCE}")"

function setup_directories () {
    mkdir -p "${PRIVATE}"
    mkdir -p "${CERTS}"
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
default_days            = 1095          # how long to certify for
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
    openssl req -new -x509 -config "${CA_HOME}/ca.cnf" -days 365  -sha512 \
        -key "${PRIVATE}/daosCA.key" \
        -out "${CERTS}/daosCA.crt" -batch
    [[ $EUID -eq 0 ]] && chown root.root "${CERTS}/daosCA.crt" 2>/dev/null
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

function cleanup () {
    rm -f "${CERTS}/*pem"
    rm -f "${CA_HOME}/agent.csr"
    rm -f "${CA_HOME}/admin.csr"
    rm -f "${CA_HOME}/server.csr"
    rm -f "${CA_HOME}/ca.cnf"
}

function main () {
    setup_directories
    generate_ca_cnf
    generate_ca_cert
    generate_server_cert
    generate_agent_cert
    generate_admin_cert
    cleanup
}

main

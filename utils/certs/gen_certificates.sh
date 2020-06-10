#!/bin/bash
# Copyright (C) 2019 Intel Corporation.
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
Usage: gen_certificates.sh [OPTIONS]
Generate certificates for DAOS deployment in the ./daosCA.
"

function print_usage () {
	>&2 echo "$__usage"
}

PRIVATE="./daosCA/private"
CERTS="./daosCA/certs"
CONFIGS="$(dirname "$BASH_SOURCE")"

function setup_directories () {
	mkdir -p ./daosCA/{certs,private}
}

function generate_ca_cert () {
	echo "Generating Private CA Root Certificate"
	# Generate Private key and set permissions
	openssl genrsa -out $PRIVATE/daosCA.key 4096
	chmod 0700 $PRIVATE/daosCA.key
	# Generate CA Certificate
	openssl req -new -x509 -config "$CONFIGS/ca.cnf" -days 1095  -sha512 \
		-key $PRIVATE/daosCA.key \
		-out $CERTS/daosCA.crt -batch
	# Reset the the CA index
	rm -f ./daosCA/index.txt ./daosCA/serial.txt
	touch ./daosCA/index.txt
	echo '01' > ./daosCA/serial.txt
	echo "Private CA Root Certificate created in ./daosCA"
}

function generate_agent_cert () {
	echo "Generating Agent Certificate"
	# Generate Private key and set its permissions
	openssl genrsa -out $CERTS/agent.key 4096
	chmod 0700 $CERTS/agent.key
	# Generate a Certificate Signing Request (CRS)
	openssl req -new -config "$CONFIGS/agent.cnf" -key $CERTS/agent.key \
		-out agent.csr -batch
	# Create Certificate from request
	openssl ca -config "$CONFIGS/ca.cnf" -keyfile $PRIVATE/daosCA.key \
		-cert $CERTS/daosCA.crt -policy signing_policy \
		-extensions signing_agent -out $CERTS/agent.crt \
		-outdir $CERTS -in agent.csr -batch

	echo "Required Agent Certificate Files:
	$CERTS/daosCA.crt
	$CERTS/agent.key
	$CERTS/agent.crt"
}

function generate_admin_cert () {
	echo "Generating Admin Certificate"
	# Generate Private key and set its permissions
	openssl genrsa -out $CERTS/admin.key 4096
	chmod 0700 $CERTS/admin.key
	# Generate a Certificate Signing Request (CRS)
	openssl req -new -config "$CONFIGS/admin.cnf" -key $CERTS/admin.key \
		-out admin.csr -batch
	# Create Certificate from request
	openssl ca -config "$CONFIGS/ca.cnf" -keyfile $PRIVATE/daosCA.key \
		-cert $CERTS/daosCA.crt -policy signing_policy \
		-extensions signing_admin -out $CERTS/admin.crt \
		-outdir $CERTS -in admin.csr -batch

	echo "Required Admin Certificate Files:
	$CERTS/daosCA.crt
	$CERTS/admin.key
	$CERTS/admin.crt"
}

function generate_server_cert () {
	echo "Generating Server Certificate"
	# Generate Private key and set its permissions
	openssl genrsa -out $CERTS/server.key 4096
	chmod 0700 $CERTS/server.key
	# Generate a Certificate Signing Request (CRS)
	openssl req -new -config "$CONFIGS/server.cnf" -key $CERTS/server.key \
		-out server.csr -batch
	# Create Certificate from request
	openssl ca -config "$CONFIGS/ca.cnf" -keyfile $PRIVATE/daosCA.key \
		-cert $CERTS/daosCA.crt -policy signing_policy \
		-extensions signing_server -out $CERTS/server.crt \
		-outdir $CERTS -in server.csr -batch

	echo "Required Server Certificate Files:
	$CERTS/daosCA.crt
	$CERTS/server.key
	$CERTS/server.crt"
}

function generate_test_cert () {
	# Don't try to generate the test cert in production installs.
	if ! [ -f "$CONFIGS/test.cnf" ]; then
		return
	fi

	echo "Generating Test Certificate"
	# Generate Private key and set its permissions
	openssl genrsa -out $CERTS/test.key 4096
	chmod 0700 $CERTS/test.key
	# Generate a Certificate Signing Request (CRS)
	openssl req -new -config "$CONFIGS/test.cnf" -key $CERTS/test.key \
		-out test.csr -batch
	# Create Certificate from request
	openssl ca -config "$CONFIGS/ca.cnf" -keyfile $PRIVATE/daosCA.key \
		-cert $CERTS/daosCA.crt -policy signing_policy \
		-extensions signing_test -out $CERTS/test.crt \
		-outdir $CERTS -in test.csr -batch

	echo "Required Test Certificate Files:
	$CERTS/daosCA.crt
	$CERTS/test.key
	$CERTS/test.crt"
}


function cleanup () {
	rm -f $CERTS/*pem
	rm -f agent.csr
	rm -f admin.csr
	rm -f server.csr
	rm -f test.csr
}

function fixup_permissions() {
	chmod 0700 $CERTS/*.key
	chmod 0664 $CERTS/*.crt
}

function main () {
	setup_directories
	generate_ca_cert
	generate_server_cert
	generate_agent_cert
	generate_admin_cert
	generate_test_cert
	fixup_permissions
	cleanup
}

main

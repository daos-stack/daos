#! /bin/bash
# SPDX-License-Identifier: BSD-3-Clause

. ${DIR}/trs_3descbc_sha1_common_defs.sh

SGW_CMD_XPRM="${DPDK_VARS} ${DPDK_MODE} ${SGW_CMD_XPRM}"

config_remote_xfrm_44()
{
	ssh ${REMOTE_HOST} ip xfrm policy flush
	ssh ${REMOTE_HOST} ip xfrm state flush

	ssh ${REMOTE_HOST} ip xfrm policy add \
src ${REMOTE_IPV4} dst ${LOCAL_IPV4} \
dir out ptype main action allow \
tmpl proto esp mode transport reqid 1

	ssh ${REMOTE_HOST} ip xfrm policy add \
src ${LOCAL_IPV4} dst ${REMOTE_IPV4} \
dir in ptype main action allow \
tmpl proto esp mode transport reqid 2

	ssh ${REMOTE_HOST} ip xfrm state add \
src ${REMOTE_IPV4} dst ${LOCAL_IPV4} \
proto esp spi 7 reqid 1 mode transport replay-window 64 ${XFRM_ESN} \
auth sha1 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef \
enc "cbc\(des3_ede\)" 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef

	ssh ${REMOTE_HOST} ip xfrm state add \
src ${LOCAL_IPV4} dst ${REMOTE_IPV4} \
proto esp spi 7 reqid 2 mode transport replay-window 64 ${XFRM_ESN} \
auth sha1 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef \
enc "cbc\(des3_ede\)" 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef

	ssh ${REMOTE_HOST} ip xfrm policy list
	ssh ${REMOTE_HOST} ip xfrm state list
}

config_remote_xfrm_66()
{
	ssh ${REMOTE_HOST} ip xfrm policy flush
	ssh ${REMOTE_HOST} ip xfrm state flush

	ssh ${REMOTE_HOST} ip xfrm policy add \
src ${REMOTE_IPV6} dst ${LOCAL_IPV6} \
dir out ptype main action allow \
tmpl proto esp mode transport reqid 3

	ssh ${REMOTE_HOST} ip xfrm policy add \
src ${LOCAL_IPV6} dst ${REMOTE_IPV6} \
dir in ptype main action allow \
tmpl proto esp mode transport reqid 4


	ssh ${REMOTE_HOST} ip xfrm state add \
src ${REMOTE_IPV6} dst ${LOCAL_IPV6} \
proto esp spi 9 reqid 3 mode transport replay-window 64 ${XFRM_ESN} \
auth sha1 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef \
enc "cbc\(des3_ede\)" 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef

	ssh ${REMOTE_HOST} ip xfrm state add \
src ${LOCAL_IPV6} dst ${REMOTE_IPV6} \
proto esp spi 9 reqid 4 mode transport replay-window 64 ${XFRM_ESN} \
auth sha1 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef \
enc "cbc\(des3_ede\)" 0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef

	ssh ${REMOTE_HOST} ip xfrm policy list
	ssh ${REMOTE_HOST} ip xfrm state list
}

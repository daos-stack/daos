#!/bin/bash

set -u -e -o pipefail
# set -x

for pool in $1/*-*-*-*-* ; do
	for vos in "$pool"/vos-* ; do
		cont_len=$(env PMEMOBJ_CONF="sds.at_create=0" ddb $vos ls | grep -c -e 'CONT:' || true)
		if [[ $cont_len -eq 0 ]] ; then
			continue
		fi
		for cont in $(seq 0 $(($cont_len - 1))) ; do
			if [[ $(env PMEMOBJ_CONF="sds.at_create=0" ddb $vos dtx_dump -a "/[$cont]" | grep -c -e '^ID:') == 0 ]] ; then
				continue
			fi
			env PMEMOBJ_CONF="sds.at_create=0" ddb -w $vos dtx_act_discard_invalid "/[$cont]" all
		done
	done
done

#!/bin/bash

cwd=$(dirname "$0")
DAOS_DIR=$(cd "${cwd}/../../.." && echo "$PWD")
#shellcheck disable=SC1090
source "$DAOS_DIR/.build_vars.sh"
EVT_CTL="$SL_PREFIX/bin/evt_ctl"

cmd="$EVT_CTL -C o:4"

function word_set {
    ((flag = $1 % 2))
    n=$1
    ((nm1 = n - 1))
    ((nm2 = n - 2))
    ((nm3 = n - 3))
    ((np1 = n + 1))
    ((np2 = n + 2))
    ((np3 = n + 3))
    ((np4 = n + 4))
    if [ $flag -eq 0 ]; then
        cmd+=$(cat << EOF
 -a 20-24@$n:black		\
 -a 90-95@$np2:coffee	\
 -a 50-56@$n:scarlet	\
 -a 57-61@$n:witch		\
 -a 96-99@$n:blue		\
 -a 78-82@$np2:hello	\
 -a 25-29@$np1:widow	\
 -a 10-15@$n:spider		\
 -a 35-40@$np2:yellow	\
 -a -36-41@$np2:boggle	\
 -a -34-39@$np2:dimple	\
 -a 34-38@$np3:tight	\
 -a 0-3@$nm1:bulk
EOF
)
    else
        cmd+=$(cat << EOF
 -a 20-26@$n:coveted	\
 -a 50-54@$n:fight		\
 -a 90-95@$np2:cookie	\
 -a 57-61@$n:hairy		\
 -a 96-98@$n:toe		\
 -a 78-82@$np2:yummy	\
 -a 25-29@$np1:bagel	\
 -a 10-15@$n:trucks		\
 -a 34-39@$np2:crooks	\
 -a -36-41@$np2:simple	\
 -a -35-40@$np2:bakers	\
 -a 34-38@$np3:motor	\
 -a 0-3@$nm1:cake
EOF
)
    fi

    cmd+=$(cat << EOF
 -f 20-30@$np1			\
 -f 28-52@$np1			\
 -f 20-30@$nm2			\
 -f 35-40@$np2			\
 -f 35-40@$np3			\
 -f 0-100@$nm1			\
 -f 0-100@$np2			\
 -a 3-28@$np4:abcdefghijklmnopqrstuvwxyz	\
 -a 31-56@$nm3:abcdefghijklmnopqrstuvwxyz	\
 -f 0-100@$np4
EOF
)
}

i=0
while [ $i -lt 20 ]; do
    ((base = i * 9))
    ((next = base + 2))
    word_set $next
    ((i = i + 1))
done

echo "$cmd"

$cmd -b "-1" -a "20-25@60000:finish" -D

result=$?
echo Test returned $result
exit $result

#!/bin/bash

if [ "$USE_VALGRIND" = "memcheck" ]; then
    VCMD="valgrind --leak-check=full --show-reachable=yes --error-limit=no \
          --suppressions=${VALGRIND_SUPP} --error-exitcode=42 --xml=yes \
          --xml-file=unit-test-evt_ctl-%p.memcheck.xml"
elif [ "$USE_VALGRIND" = "pmemcheck" ]; then
    VCMD="valgrind --tool=pmemcheck "
fi

cwd=$(dirname "$0")
DAOS_DIR=$(cd "${cwd}/../../.." && echo "$PWD")
#shellcheck disable=SC1090
source "$DAOS_DIR/.build_vars.sh"
EVT_CTL="$SL_PREFIX/bin/evt_ctl"


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
 -f 0-100@$np4          \
 -d -0-100@$np4
EOF
)
}

function check_max {
    first=$base
    ((second = first + 10))
    ((third = second + 10))
    ((fourth = third + 10))

    cmd+=$(cat << EOF
    -a 0-1@$first:AA            \
    -a 350-355@$first:evtree    \
    -a 355-355@$second          \
    -a 1-360@$third             \
    -a 0-0@$fourth              \
    -f 0-355@$first             \
    -f 0-355@$fourth
EOF
)
}

# Sequence
cmd="$VCMD $EVT_CTL --start-test \"evt tests $*\" $* -C o:4"

i=0
while [ $i -lt 20 ]; do
    ((base = i * 9))
    ((next = base + 4))
    word_set $next
    ((i = i + 1))
done

i=1
while [ $i -lt 20 ]; do
    ((base = i * 60000))
    check_max $base
    ((i = i + 1))
done

cmd+=$(cat << EOF
    -l              \
    -l0-100@30-50:V \
    -l0-100@30-50:v \
    -l0-100@30-50:B \
    -l0-100@30-50:b \
    -l0-100@30-50:C \
    -l0-100@30-50:c \
    -l0-100@30-50:H \
    -l0-100@30-50:h \
    -l0-1000@0:B    \
    -l0-1000@0:V    \
    -l0-1000@0:C    \
    -l0-1000@0:H    \
    -l0-100@30-50:-V \
    -l0-100@30-50:-v \
    -l0-100@30-50:-C \
    -l0-100@30-50:-c \
    -l0-100@30-50:-H \
    -l0-100@30-50:-h \
    -l0-1000@0:-V    \
    -l0-1000@0:-C    \
    -l0-1000@0:-H    \
    -d 20-24@4   \
    -d 90-95@6   \
    -d 50-56@4   \
    -d 57-61@4   \
    -d 96-99@4   \
    -d 78-82@6   \
    -d 25-29@5   \
    -d 10-15@4   \
    -d 35-40@6   \
    -d 20-24@22  \
    -d 90-95@24  \
    -d 50-56@22  \
    -d 57-61@22  \
    -d 96-99@22  \
    -d 78-82@24  \
    -d 25-29@23  \
    -d 10-15@22  \
    -d 35-40@24  \
    -d 35-40@42
EOF
)

cmd+=" -b -2 -D"
cmd+=" -C o:5 -a 1-8@1.1:12345678 -a 0-1@1.2 -a 8-9@1.3 -a 5-6@1.4:ab"
cmd+=" -l0-10@0-1 -f 0-10@1 -f 0-10@2 -l0-10@0-1:b -a 0-8589934592@2 -f 0-10@3"
cmd+=" -a 1-3@3:aaa -f 0-10@4 -d 0-1@1.2 -f 0-10@4 -d 0-8589934592@2"
cmd+=" -f 0-10@4 -a 0-562949953421312@5 -f 0-10@5 -b -2 -D"
cmd+=" -C o:4 -a 0-1@1:ab -a 2-3@1:ab -a 4-5@1:ab -a 6-7@1:ab"
cmd+=" -a 9223372036854775808-9223372036854775809@1:ab -b -2 -D"
cmd+=" -C o:5 -a 0-1@1:ab -a 1-2@2:cd -a 3-4@3:bc -a 5-7@4:def -a 6-8@5:xyz"
cmd+=" -a 1-2@6:aa -a 4-7@7:abcd -b -2 -r -0-5@8 -b -2 -r 0-5@3 -b -2 -D"
echo "$cmd"
eval "$cmd"
result="${PIPESTATUS[0]}"
echo "Test returned $result"
if (( result != 0 )); then
        exit "$result"
fi

# Internal tests
cmd="$VCMD $EVT_CTL --start-test \"evtree built-in tests $*\" $* -t"
echo "$cmd"
eval "$cmd"
result="${PIPESTATUS[0]}"
echo "Internal tests returned $result"
if (( result != 0 )); then
        exit "$result"
fi

# Drain tests
cmd="$VCMD $EVT_CTL --start-test \"evtree drain tests $*\" $* -C o:4"
cmd+=" -e s:0,e:128,n:2379 -c"
echo "$cmd"
eval "$cmd"
result="${PIPESTATUS[0]}"
echo "Drain test returned $result"
exit "$result"

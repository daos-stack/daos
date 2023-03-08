# shellcheck shell=bash

exec 2>/dev/null

set -eu

core_pattern=$(cat /proc/sys/kernel/core_pattern)

# CI defined/overridden core path:
if [ "$core_pattern" = "/var/tmp/core.%e.%t.%p" ]; then
    echo "$core_pattern"
    exit 0
fi

# systemd-coredump core path
if [[ "$core_pattern" = *systemd-coredump* ]]; then
    # move the core files where our processing scripts wants to find them
    coredumpctl list -S @"$1" --no-legend |
      while read -r d dd t tz pid _uid _gid _sig _status exe; do
        if [[ $exe = */gdb ]]; then
            continue
        fi
        coredumpctl dump --output \
                    /var/tmp/core."${exe##*/}"."$(date +%s -d"$d $dd $t $tz")"."$pid" "$pid"
    done
fi

# report the path we moved them to
echo "/var/tmp/core.%e.%t.%p"
exit 0

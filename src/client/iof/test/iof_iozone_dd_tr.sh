#!/bin/sh

# NOTE: This shell script is used in iof_iozone_cmds.yml as an alternative.

dd status=none bs=512k count=1 if=/dev/zero | tr '\0' '\72' > $1

#!/bin/bash
set -xe

# Script to test existence or non-existence of a file

if [ "$1" = "-n" ]; then
   shift
   for file in "$@"; do
       if [ -f "$file" ]; then
           echo "$file should not exist but does."
           exit 1
       fi
   done
else
   for file in "$@"; do
       if [ ! -f "$file" ]; then
           echo "$file should exist but doesn't."
           exit 1
       fi
   done
fi

exit 0

#!/bin/bash

# Script to test existence or non-existence of a file

if [ "$1" = "-n" ]; then
   shift
   for file in "$@"; do
       if [ -f "$file" ]; then
           exit 1
       fi
   done
else
   for file in "$@"; do
       if [ ! -f "$file" ]; then
           exit 1
       fi
   done
fi
 
exit 0

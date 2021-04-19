#!/bin/bash

set -eux

toupper() {
    echo "$1" | tr "[:lower:]" "[:upper:]"
}

stacktrace() {

   local i=0

   local line func file
   while true; do
      read -r line func file < <(caller $i)
      if [ -z "$line" ]; then
          # prevent cascading ERRs
          trap '' ERR
          break
      fi
      if [ $i -eq 0 ]; then
          echo -e "Unchecked error condition at: \c"
      else
          echo -e "Called from: \c"
      fi
      echo "$file:$line $func()"
      ((i++)) || true
   done
}

set -E
trap stacktrace ERR

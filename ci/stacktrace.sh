#!/bin/bash

stacktrace() {
   local i=0
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
      ((i++))
   done
}

trap 'stacktrace' ERR

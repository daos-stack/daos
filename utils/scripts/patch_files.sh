#!/bin/bash
# Removes static libraries and and prefixes library names matching
# lib${prefix}* with libdaos${prefix}*.  Modifies the libraries
# to change dependencies accordingly.
set -x
bindir="$1"; shift
libdir="$1"; shift

mkdir -p "${libdir}/daos_internal"

for dir in "${bindir}" "${libdir}"; do
  all_files=("$(find ${dir} -type l,f)")
  files=()
  for file in ${all_files[@]}; do
    lib=$(basename ${file})
    removed=0
    for prefix in $@; do
      if [[ ${lib} =~ lib${prefix}.*\.a$ ]]; then
        rm -f "${file}"
        removed=1
        break
      fi
    done
    if [ ${removed} -eq 0 ]; then
      files+=("${file}")
    fi
  done

  for file in ${files[@]}; do
    lib="$(basename ${file})"
    dirname="$(dirname ${file})"
    newfile=""
    for prefix in $@; do
      newlib="${lib//lib${prefix}/libdaos${prefix}}"
      if [ "$newlib" = "${lib}" ]; then
        newlib="${lib//${prefix}/daos${prefix}}"
      fi
      if [ "$newlib" != "$lib" ]; then
        if [ -L "${file}" ]; then
          target="$(readlink ${file})"
          newtarget="${target//lib${prefix}/libdaos${prefix}}"
          ln -s "${newtarget}" "${dirname}/${newlib}"
          #create an extra link for internal use
          ln -s "../${newtarget}" "${dirname}/daos_internal/${lib}"
          rm -f "${file}"
        else
          newfile="${dirname}/${newlib}"
          mv "${file}" "${newfile}"
        fi
        break
      fi
    done
    if [ -n "${newfile}" ]; then
      for prefix in $@; do
        if [[ ${lib} =~ .*${prefix}.*\.pc$ ]]; then
          sed -i "s/${prefix}/daos${prefix}/g" "${newfile}"
        elif [ -n $(grep -IL '' "$newfile") ]; then
          libs=($(patchelf --print-needed "${newfile}")) 2>/dev/null
          if [ $? -eq 0 ]; then
            for old in ${libs[@]}; do
              replace="${old//lib${prefix}/libdaos${prefix}}"
              if [ "${replace}" != "${old}" ]; then
                patchelf --replace-needed "${old}" "${replace}" "${newfile}"
              fi
            done
          fi
          soname=$(patchelf --print-soname "${newfile}") 2>/dev/null
          if [ $? -eq 0 ]; then
            replace="${soname//lib${prefix}/libdaos${prefix}}"
            if [ "${replace}" != "${soname}" ]; then
              patchelf --set-soname "${replace}" "${newfile}"
            fi
          fi
        fi
      done
    fi
  done
done

#!/bin/bash
# Removes static libraries and and prefixes library names matching
# lib${prefix}* with libdaos${prefix}*.  Modifies the libraries
# to change dependencies accordingly.
bindir="$1"; shift
libdir="$1"; shift

mkdir -p "${libdir}/daos_internal"

for dir in "${bindir}" "${libdir}"; do
  readarray -d '' all_files < <(find "${dir}" -type l,f -print0)
  files=()
  for file in "${all_files[@]}"; do
    echo "Checking ${file} if .a"
    lib="$(basename "${file}")"
    removed=0
    for prefix in "$@"; do
      if [[ ${lib} =~ lib${prefix}.*\.a$ ]]; then
        echo "Removing ${file}"
        rm -f "${file}"
        removed=1
        break
      fi
    done
    if [ ${removed} -eq 0 ]; then
      files+=("${file}")
    fi
  done

  for file in "${files[@]}"; do
    echo "Checking ${file} for name changes"
    lib="$(basename "${file}")"
    dirname="$(dirname "${file}")"
    newfile=""
    for prefix in "$@"; do
      newlib="${lib//lib${prefix}/libdaos${prefix}}"
      if [ "$newlib" = "${lib}" ]; then
        newlib="${lib//${prefix}/daos${prefix}}"
      fi
      if [ "$newlib" != "$lib" ]; then
        if [ -L "${file}" ]; then
          target="$(readlink "${file}")"
          newtarget="${target//lib${prefix}/libdaos${prefix}}"
          if [ "${newtarget}" != "${target}" ]; then
            ln -s "${newtarget}" "${dirname}/${newlib}"
            #create an extra link for internal use
            ln -s "../${newtarget}" "${dirname}/daos_internal/${lib}"
            rm -f "${file}"
          fi
        else
          newfile="${dirname}/${newlib}"
          mv "${file}" "${newfile}"
        fi
        break
      fi
    done
    if [ -n "${newfile}" ]; then
      for prefix in "$@"; do
        if [[ ${lib} =~ .*${prefix}.*\.pc$ ]]; then
          sed -i "s/${prefix}/daos${prefix}/g" "${newfile}"
        elif ! grep -qI '' "$newfile"; then
          readarray libs < <(patchelf --print-needed "${newfile}" 2>/dev/null)
          for old in "${libs[@]}"; do
            old="$(echo "${old}" | xargs echo -n)"
            replace="${old//lib${prefix}/libdaos${prefix}}"
            echo "Checking ${newfile} for ${prefix}: ${old} -> ${replace}"
            if [ "${replace}" != "${old}" ]; then
              echo "patchelf --replace-needed ${old} ${replace} ${newfile}"
              patchelf --replace-needed "${old}" "${replace}" "${newfile}"
            fi
          done
          if soname="$(patchelf --print-soname "${newfile}" 2>/dev/null)"; then
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

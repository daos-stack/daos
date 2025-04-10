#!/bin/bash
# Removes static libraries and and prefixes library names matching
# lib${prefix}* with libdaos${prefix}*.  Modifies the libraries
# to change dependencies accordingly.
bindir="$1"; shift
libdir="$1"; shift

mkdir -p "${libdir}/daos_internal"
#create links first
readarray -d '' links < <(find "${libdir}" -type l -print0)
for file in "${links[@]}"; do
  target="$(readlink "${file}")"
  echo "Checking to see if link ${file} -> ${target} needs fixing"
  lib="$(basename "${file}")"
  if [[ "${lib}" =~ daos ]]; then
    continue
  fi
  for prefix in "$@"; do
      newtarget="${target//lib${prefix}/libdaos${prefix}}"
      echo "Checking ${newtarget}"
      if [ "${newtarget}" != "${target}" ]; then
        # create a link from original filename in daos_internal to the
        # target.  This way link time name is unchanged for DAOS build.
        echo "Creating link ${libdir}/daos_internal/${lib} -> ${newtarget}"
        ln -s ../"${newtarget}" "${libdir}/daos_internal/${lib}"
        newlib="${lib//lib${prefix}/libdaos${prefix}}"
        # Now replace the link
        ln -s "${newtarget}" "$(dirname "${file}")/${newlib}"
        rm "${file}"
        break
      fi
  done
done

for dir in "${bindir}" "${libdir}"; do
  readarray -d '' all_files < <(find "${dir}" -type f -print0)
  files=()
  for file in "${all_files[@]}"; do
    if [[ "${file}" =~ daos_internal ]]; then
      continue
    fi
    echo "Checking ${file} if .a"
    lib="$(basename "${file}")"
    if [[ "${lib}" =~ daos ]]; then
      continue
    fi
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
    if [[ "${lib}" =~ daos ]]; then
      continue
    fi
    dirname="$(dirname "${file}")"
    newfile=""
    for prefix in "$@"; do
      newlib="${lib//lib${prefix}/libdaos${prefix}}"
      if [ "$newlib" = "${lib}" ]; then
        newlib="${lib//${prefix}/daos${prefix}}"
      fi
      if [ "$newlib" != "$lib" ]; then
        if [ -L "${file}" ]; then
          rm -f "${file}"
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
          continue
        elif ! grep -qI '' "$newfile"; then
          readarray libs < <(patchelf --print-needed "${newfile}" 2>/dev/null)
          for old in "${libs[@]}"; do
            old="$(echo "${old}" | xargs echo -n)"
            replace="${old//lib${prefix}/libdaos${prefix}}"
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

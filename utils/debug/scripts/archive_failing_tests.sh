#!/bin/bash

# set -x
set -e -o pipefail

tmp_dir="$1"

rm -fr "$tmp_dir"
mkdir -p "$tmp_dir"

echo "[INFO] Decompressing bzip2 logs"
find "${@:2}" -type f -name "*.bz2" -exec bunzip2 -v {} \;

echo "[INFO] Searching failing tests"
find "${@:2}" -type f -name debug.log | while read file_path ; do
	if tail -2 "$file_path" | head -1 | grep -q -v -e ' INFO | PASS ' -e ' ERROR| SKIP ' -e 'ERROR| WARN ' ; then
		echo "[INFO]    - $file_path"
		result_path="$(dirname "$file_path")"
		test_path="$(dirname "$(dirname "$result_path")")"
		if [[ ! -d "$tmp_dir/$test_path" ]] ; then
			mkdir -p "$tmp_dir/$(dirname "$test_path")"
			cp -a "$test_path" "$tmp_dir/$test_path"
			rm -fr "$tmp_dir/$test_path/test-results"
			mkdir -p "$tmp_dir/$test_path/test-results"
		fi
		cp -a "$result_path" "$tmp_dir/$result_path"
	fi
done

echo "[INFO] Creating Compressed Archive"
tmp_size=$(du -ks "$tmp_dir" | awk -e '{print $1}')k
/bin/tar c "$tmp_dir" | pv -p -s $tmp_size | /bin/xz -z -9 -c - > "$1.txz"

rm -fr  "$1"

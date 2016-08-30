#!/bin/sh

if [ ! -d "scons_local" ];then
  cd ..
fi

flist="-s SConstruct -s src/SConscript -s src/crt/SConscript"
flist+=" -s src/utest/SConscript -s src/crt/test/SConscript -s test/SConscript"

for f in test/*.py ; do
  [[ -f "$f" ]] || continue
  flist+=" -P3 $f"
done

./scons_local/check_python.sh $flist

if [ $? -ne 0 ]; then
  exit 1
fi
exit 0

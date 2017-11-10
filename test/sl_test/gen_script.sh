#!/bin/sh
target=$1
shift
expected=$*

cat > ${target}.sh << EOF
#!/bin/sh
for lib in $expected; do
  echo "Ensuring \$lib is present in $target"
  ldd $target | grep \$lib
  if [ \$? -ne 0 ]; then
    echo "Missing expected lib: \$lib"
    exit 1
  fi
done
echo "Ensuring sl_project libs are absent in $target"
ldd $target | grep sl_project
if [ \$? -eq 0 ]; then
  echo "No sl_project lib should be present"
  exit 1
fi
exit 0
EOF
chmod 755 ${target}.sh


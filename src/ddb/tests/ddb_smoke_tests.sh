# -----------------------------------------------------------------------------
# Utility functions
# -----------------------------------------------------------------------------
function title() {
  LINE="======================================================================="
    echo -e "\e[1m\e[94m${LINE}"
    echo "$1"
    echo -e "${LINE}\e[0m"
}

function p() {
   if [ "$v_pause" == "1" ]; then
    read -r var
    if [ "$var" == "c" ]; then
        v_pause=0
    fi
   fi
}

function pause() {
  echo -e "\e[1m\e[94m==> $*\e[0m"
  p
}
function msg() {
   echo -e "\e[1m\e[94m*** $* ***\e[0m"
}

function echo_cmd() {
  echo -e "\e[1m\e[32m"
  echo -e "\$ " "$@"
  echo -e "\e[0m"
  p
}

function run_cmd() {
  echo_cmd "$@"
  "$@"
  echo ""
  p
}

# create a vos file to connect to
run_cmd ddb_tests -c
vos_file=/mnt/daos/12345678-1234-1234-1234-123456789012/ddb_vos_test

msg "'ls' commands"
run_cmd ddb $vos_file -R 'ls'
run_cmd ddb $vos_file -R 'ls 12345678-1234-1234-1234-123456789001'
run_cmd ddb $vos_file -R 'ls [0]'
run_cmd ddb $vos_file -R 'ls [0]/[1]'
run_cmd ddb $vos_file -R 'ls [0]/[1] -r'

msg "'dump' and 'load' commands"
vos_path="[0]/[0]/[0]/[1]"
echo 'echo "A New Value" > /tmp/ddb_new_value'
echo "A New Value" > /tmp/ddb_new_value

run_cmd ddb $vos_file -R "dump_value $vos_path /tmp/ddb_value_dump"
run_cmd cat /tmp/ddb_value_dump
run_cmd ddb $vos_file -R "dump_value [0]/[0]/[0]/[0]/[0] /tmp/ddb_value_dump"
run_cmd cat /tmp/ddb_value_dump

run_cmd ddb $vos_file -R "load /tmp/ddb_new_value $vos_path 2"
run_cmd ddb $vos_file -R "dump_value $vos_path /tmp/ddb_value_dump"
run_cmd cat /tmp/ddb_value_dump
run_cmd diff /tmp/ddb_new_value /tmp/ddb_value_dump

msg "'load' to new key"
vos_path="[0]/[0]/[0]/\'new_new_new_new_key\'"

run_cmd ddb $vos_file -R "load /tmp/ddb_new_value $vos_path 1"
run_cmd ddb $vos_file -R "dump_value $vos_path /tmp/ddb_value_dump"
run_cmd cat /tmp/ddb_value_dump
run_cmd ddb $vos_file -R 'ls [0]/[0]/[0] -r'
diff /tmp/ddb_new_value /tmp/ddb_value_dump

msg "'superblock', 'ilog' and 'dtx' dumps"
run_cmd ddb $vos_file -R 'dump_superblock'
run_cmd ddb $vos_file -R 'dump_ilog [0]/[0]'
run_cmd ddb $vos_file -R 'dump_ilog [0]/[0]/[0]'

run_cmd ddb $vos_file -R 'dump_dtx [0]'

msg "'rm'"
run_cmd ddb $vos_file -R 'ls'
run_cmd ddb $vos_file -R 'rm [1]'
run_cmd ddb $vos_file -R 'ls'

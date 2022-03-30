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
run_cmd ddb $vos_file -R 'ls'
run_cmd ddb $vos_file -R 'ls 12345678-1234-1234-1234-123456789001'
run_cmd ddb $vos_file -R 'ls [0]'
run_cmd ddb $vos_file -R 'ls [0]/[1]'
run_cmd ddb $vos_file -R 'ls -r'

#!/bin/bash
#
# Runs Terraform to create DAOS Server and Client instances.
# Copies necessary files to clients to allow the IO500 benchmark to be run.
#
# Since some GCP projects are not set up to use os-login this script generates
# an SSH for the daos-user account that exists in the instances. You can then
# use the generated key to log into the first daos-client instance which
# is used as a bastion host.
#

set -e
trap 'echo "Hit an unexpected and unchecked error. Exiting."' ERR

SCRIPT_NAME="$(basename "$0")"
SCRIPT_DIR="$( cd "$( dirname "$0" )" && pwd )"

# Directory where all generated files will be stored
IO500_TMP="${SCRIPT_DIR}/tmp"

# Directory containing config files
CONFIG_DIR="${CONFIG_DIR:-${SCRIPT_DIR}/config}"

# Config file in ./config that is used to spin up the environment and configure IO500
CONFIG_FILE="${CONFIG_FILE:-${CONFIG_DIR}/config.sh}"

# active_config.sh is a symlink to the last config file used by start.sh
ACTIVE_CONFIG="${CONFIG_DIR}/active_config.sh"

# SSH config file path
# We generate an SSH config file that is used with 'ssh -F' to simplify logging
# into the first DAOS client instance. The first DAOS client instance is our
# bastion host for the IO500 example.
SSH_CONFIG_FILE="${IO500_TMP}/ssh_config"

ERROR_MSGS=()


show_help() {
  cat <<EOF

Usage:

  ${SCRIPT_NAME} <options>

  Set up DAOS server and client images in GCP that are capable of running the
  IO500 benchmark.

Options:

  [ -c --config   CONFIG_FILE ]   Path to a configuration file.
                                  See files in ./config
                                  Default: ./config/config.sh

  [ -v --version  DAOS_VERSION ]  Version of DAOS to install

  [ -u --repo-baseurl DAOS_REPO_BASE_URL ] Base URL of a repo.

  [ -f --force ]                  Force images to be re-built

  [ -h --help ]                   Show help

Examples:

  Deploy a DAOS environment with a specifc configuration

    ${SCRIPT_NAME} -c ./config/config_1c_1s_8d.sh

EOF
}

log() {
  msg="$1"
  print_lines="$2"
  # shellcheck disable=SC2155,SC2183
  local line=$(printf "%80s" | tr " " "-")
  if [[ -t 1 ]]; then tput setaf 14; fi
  if [[ "${print_lines}" == 1 ]]; then
    printf -- "\n%s\n %-78s \n%s\n" "${line}" "${msg}" "${line}"
  else
    printf -- "\n%s\n\n" "${msg}"
  fi
  if [[ -t 1 ]]; then tput sgr0; fi
}

log_error() {
  # shellcheck disable=SC2155,SC2183
  if [[ -t 1 ]]; then tput setaf 160; fi
  printf -- "\n%s\n\n" "${1}" >&2;
  if [[ -t 1 ]]; then tput sgr0; fi
}

log_section() {
  log "$1" "1"
}

show_errors() {
  # If there are errors, print the error messages and exit
  if [[ ${#ERROR_MSGS[@]} -gt 0 ]]; then
    printf "\n" >&2
    log_error "${ERROR_MSGS[@]}"
    show_help
    exit 1
  fi
}

check_dependencies() {
  # Exit if gcloud command not found
  if ! gcloud -v &> /dev/null; then
    log_error "ERROR: 'gcloud' command not found
       Is the Google Cloud Platform SDK installed?
       See https://cloud.google.com/sdk/docs/install"
    exit 1
  fi
  # Exit if terraform command not found
  if ! terraform -v &> /dev/null; then
    log_error "ERROR: 'terraform' command not found
       Is Terraform installed?"
    exit 1
  fi
}

opts() {

  # shift will cause the script to exit if attempting to shift beyond the
  # max args.  So set +e to continue processing when shift errors.
  set +e
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --config|-c)
        CONFIG_FILE="$2"
        if [[ "${CONFIG_FILE}" == -* ]] || [[ "${CONFIG_FILE}" == "" ]] || [[ -z ${CONFIG_FILE} ]]; then
          ERROR_MSGS+=("ERROR: Missing CONFIG_FILE value for -c or --config")
          break
        elif [[ ! -f "${CONFIG_FILE}" ]]; then
          ERROR_MSGS+=("ERROR: Configuration file '${CONFIG_FILE}' not found.")
        fi
        export CONFIG_FILE
        shift 2
      ;;
      --version|-v)
        DAOS_VERSION="${2}"
        if [[ "${DAOS_VERSION}" == -* ]] || [[ "${DAOS_VERSION}" = "" ]] || [[ -z ${DAOS_VERSION} ]]; then
          log_error "ERROR: Missing DAOS_VERSION value for -v or --version"
          show_help
          exit 1
        fi
        export DAOS_VERSION
        shift 2
      ;;
      --repo-baseurl|-u)
        DAOS_REPO_BASE_URL="${2}"
        if [[ "${DAOS_REPO_BASE_URL}" == -* ]] || [[ "${DAOS_REPO_BASE_URL}" = "" ]] || [[ -z ${DAOS_REPO_BASE_URL} ]]; then
          log_error "ERROR: Missing URL value for -u or --repo-baseurl"
          show_help
          exit 1
        fi
        export DAOS_REPO_BASE_URL
        shift 2
      ;;
      --force|-f)
        FORCE_REBUILD=1
        export FORCE_REBUILD
        shift
      ;;
      --help|-h)
        show_help
        exit 0
      ;;
	    --*|-*)
        ERROR_MSGS+=("ERROR: Unrecognized option '${1}'")
        shift
        break
      ;;
	    *)
        ERROR_MSGS+=("ERROR: Unrecognized option '${1}'")
        shift
        break
      ;;
    esac
  done
  set -e

  show_errors
}

create_active_config_symlink() {
  # Create a ${IO500_TMP}/config/active_config.sh symlink that points to the
  # config file that is being used now. This is needed so that the stop.sh can
  # always source the same config file that was used in start.sh
  if [[ -L "${ACTIVE_CONFIG}" ]]; then
    current_config=$(readlink "${ACTIVE_CONFIG}")
    if [[ "$(basename ${CONFIG_FILE})" != $(basename "${current_config}") ]]; then
       read -d '' err_msg <<EOF || true
ERROR
Cannot use configuration: ${CONFIG_FILE}
An active configuration already exists: ${current_config}

You must run

  ${SCRIPT_NAME} -c ${current_config}

or run the stop.sh script before running

  ${SCRIPT_NAME} -c ${CONFIG_FILE}
EOF
      log_error "${err_msg}"
      exit 1
    fi
  else
    ln -snf "${CONFIG_FILE}" "${CONFIG_DIR}/active_config.sh"
  fi
}

load_config() {
  # Load configuration which contains all settings for Terraform and the IO500
  # benchmark
  log "Sourcing config file: ${CONFIG_FILE}"
  source "${CONFIG_FILE}"
}

create_hosts_files() {

  # pdsh or clush commands will need to be run from the first daos-client
  # instance. Those commands will need to take a file which contains a list of
  # hosts.  This function creates 3 files:
  #    hosts_clients - a list of daos-client* hosts
  #    hosts_servers - a list of daos-server* hosts
  #    hosts_all     - a list of all hosts
  # The copy_files_to_first_client function in this script will copy the hosts_* files to
  # the first daos-client instance.

  unset CLIENTS
  unset SERVERS
  unset ALL_NODES

  mkdir -p "${IO500_TMP}"
  HOSTS_CLIENTS_FILE="${IO500_TMP}/hosts_clients"
  HOSTS_SERVERS_FILE="${IO500_TMP}/hosts_servers"
  HOSTS_ALL_FILE="${IO500_TMP}/hosts_all"

  rm -f "${HOSTS_CLIENTS_FILE}" "${HOSTS_SERVERS_FILE}" "${HOSTS_ALL_FILE}"

  for ((i=1; i<=${DAOS_CLIENT_INSTANCE_COUNT}; i++))
  do
      CLIENTS+="${DAOS_CLIENT_BASE_NAME}-$(printf %04d ${i}) "
      echo ${DAOS_CLIENT_BASE_NAME}-$(printf %04d ${i})>>"${HOSTS_CLIENTS_FILE}"
      echo ${DAOS_CLIENT_BASE_NAME}-$(printf %04d ${i})>>"${HOSTS_ALL_FILE}"
  done

  for ((i=1; i<=${DAOS_SERVER_INSTANCE_COUNT}; i++))
  do
      SERVERS+="${DAOS_SERVER_BASE_NAME}-$(printf %04d ${i}) "
      echo ${DAOS_SERVER_BASE_NAME}-$(printf %04d ${i})>>"${HOSTS_SERVERS_FILE}"
      echo ${DAOS_SERVER_BASE_NAME}-$(printf %04d ${i})>>"${HOSTS_ALL_FILE}"
  done

  DAOS_FIRST_CLIENT=$(echo ${CLIENTS} | awk '{print $1}')
  DAOS_FIRST_SERVER=$(echo ${SERVERS} | awk '{print $1}')
  ALL_NODES="${SERVERS} ${CLIENTS}"

  export CLIENTS
  export DAOS_FIRST_CLIENT
  export HOSTS_CLIENTS_FILE
  export SERVERS
  export DAOS_FIRST_SERVER
  export HOSTS_SERVERS_FILE
  export ALL_NODES

}

build_disk_images() {
  # Build the DAOS disk images
  "${SCRIPT_DIR}/build_daos_io500_images.sh" --type all
}

run_terraform() {
  log_section "Deploying DAOS Servers and Clients using Terraform"
  pushd ../daos_cluster
  terraform init -input=false
  terraform plan -out=tfplan -input=false
  terraform apply -input=false tfplan
  popd
}

configure_first_client_nat_ip() {

  log "Wait for DAOS client instances"
  gcloud compute instance-groups managed wait-until ${TF_VAR_client_template_name} \
    --stable \
    --project="${TF_VAR_project_id}" \
    --zone="${TF_VAR_zone}"

  # Check to see if first client instance has an external IP.
  # If it does, then don't attempt to add an external IP again.
  FIRST_CLIENT_IP=$(gcloud compute instances describe "${DAOS_FIRST_CLIENT}" \
    --project="${TF_VAR_project_id}" \
    --zone="${TF_VAR_zone}" \
    --format="value(networkInterfaces[0].accessConfigs[0].natIP)")

  if [[ -z "${FIRST_CLIENT_IP}" ]]; then
    log "Add external IP to first client"

    gcloud compute instances add-access-config "${DAOS_FIRST_CLIENT}" \
      --project="${TF_VAR_project_id}" \
      --zone="${TF_VAR_zone}" \
      && sleep 10

    FIRST_CLIENT_IP=$(gcloud compute instances describe "${DAOS_FIRST_CLIENT}" \
      --project="${TF_VAR_project_id}" \
      --zone="${TF_VAR_zone}" \
      --format="value(networkInterfaces[0].accessConfigs[0].natIP)")
  fi
}

configure_ssh() {
  # TODO: Need improvements here.
  #       Using os_login is preferred but after some users ran into issues with it
  #       this turned out to be the method that worked for most users.
  #       This function generates a key pair and an ssh config file that is
  #       used to log into the first daos-client node as the 'daos-user' user.
  #       This isn't ideal in team situations where a team member who was not
  #       the one who ran this start.sh script needs to log into the instances
  #       as the 'daos-user' in order to run IO500 or do troubleshooting.
  #       If os-login was used, then project admins would be able to control
  #       who has access to the daos-* instances. Users would access the daos-*
  #       instances the same way they do all other instances in their project.

  log_section "Configure SSH on first client instance ${DAOS_FIRST_CLIENT}"

  # Create an ssh key for the current IO500 example environment
  if [[ ! -f "${IO500_TMP}/id_rsa" ]]; then
    log "Generating SSH key pair"
    ssh-keygen -t rsa -b 4096 -C "${SSH_USER}" -N '' -f "${IO500_TMP}/id_rsa"
  fi
  chmod 600 "${IO500_TMP}/id_rsa"

  if [[ ! -f "${IO500_TMP}/id_rsa.pub" ]]; then
    log_error "Missing file: ${IO500_TMP}/id_rsa.pub"
    log_error "Unable to continue without id_rsa and id_rsa.pub files in ${IO500_TMP}"
    exit 1
  fi

  # Generate file containing keys which will be added to the metadata of all nodes.
  echo "${SSH_USER}:$(cat ${IO500_TMP}/id_rsa.pub)" > "${IO500_TMP}/keys.txt"

  # Only update instance meta-data once
  if ! gcloud compute instances describe "${DAOS_FIRST_CLIENT}" \
    --project="${TF_VAR_project_id}" \
    --zone="${TF_VAR_zone}" \
    --format='value[](metadata.items.ssh-keys)' | grep -q "${SSH_USER}"; then

    log "Disable os-login and add '${SSH_USER}' SSH key to metadata on all instances"
    for node in ${ALL_NODES}; do
      echo "Updating metadata for ${node}"
      # Disable OSLogin to be able to connect with SSH keys uploaded in next command
      gcloud compute instances add-metadata "${node}" \
        --project="${TF_VAR_project_id}" \
        --zone="${TF_VAR_zone}" \
        --metadata enable-oslogin=FALSE && \
      # Upload SSH key to instance, so that you can log into instance via SSH
      gcloud compute instances add-metadata "${node}" \
        --project="${TF_VAR_project_id}" \
        --zone="${TF_VAR_zone}" \
        --metadata-from-file ssh-keys="${IO500_TMP}/keys.txt" &
    done
    # Wait for instance meta-data updates to finish
    wait
  fi

  # Create ssh config for all instances
  cat > "${IO500_TMP}/instance_ssh_config" <<EOF
Host *
    CheckHostIp no
    UserKnownHostsFile /dev/null
    StrictHostKeyChecking no
    IdentityFile ~/.ssh/id_rsa
    IdentitiesOnly yes
    LogLevel ERROR
EOF
  chmod 600 "${IO500_TMP}/instance_ssh_config"

  # Create local ssh config
  cat > "${SSH_CONFIG_FILE}" <<EOF
Include ~/.ssh/config
Include ~/.ssh/config.d/*

Host ${FIRST_CLIENT_IP}
    CheckHostIp no
    UserKnownHostsFile /dev/null
    StrictHostKeyChecking no
    IdentitiesOnly yes
    LogLevel ERROR
    User ${SSH_USER}
    IdentityFile ${IO500_TMP}/id_rsa

EOF
  chmod 600 "${SSH_CONFIG_FILE}"

  log "Copy SSH key to first DAOS client instance ${DAOS_FIRST_CLIENT}"

  # Create ~/.ssh directory on first daos-client instance
  ssh -q -F "${SSH_CONFIG_FILE}" ${FIRST_CLIENT_IP} \
    "mkdir -m 700 -p ~/.ssh"

  # Copy SSH key pair to first daos-client instance
  scp -q -F "${SSH_CONFIG_FILE}" \
    ${IO500_TMP}/id_rsa \
    ${IO500_TMP}/id_rsa.pub \
    "${FIRST_CLIENT_IP}:~/.ssh/"

  # Copy SSH config to first daos-client instance and set permissions
  scp -q -F "${SSH_CONFIG_FILE}" \
    "${IO500_TMP}/instance_ssh_config" \
    "${FIRST_CLIENT_IP}:~/.ssh/config"
  ssh -q -F "${SSH_CONFIG_FILE}" "${FIRST_CLIENT_IP}" \
    "chmod -R 600 ~/.ssh/*"

}

copy_files_to_first_client() {
  # Copy the files that will be needed in order to run pdsh, clush and other
  # commands on the first daos-client instance

  log "Copy files to first client ${DAOS_FIRST_CLIENT}"

  # Copy the config file for the IO500 example environment
  scp -F "${SSH_CONFIG_FILE}" \
    "${CONFIG_FILE}" \
    "${SSH_USER}"@"${FIRST_CLIENT_IP}":~/config.sh

  scp -F "${SSH_CONFIG_FILE}" \
    "${HOSTS_CLIENTS_FILE}" \
    "${HOSTS_SERVERS_FILE}" \
    "${HOSTS_ALL_FILE}" \
    ${SCRIPT_DIR}/configure_daos.sh \
    ${SCRIPT_DIR}/clean_storage.sh \
    ${SCRIPT_DIR}/run_io500-sc21.sh \
    "${FIRST_CLIENT_IP}:~/"

  ssh -q -F "${SSH_CONFIG_FILE}" ${FIRST_CLIENT_IP} \
    "chmod +x ~/*.sh && chmod -x ~/config.sh"

}

propagate_ssh_keys_to_all_nodes () {
  # Clear ~/.ssh/known_hosts so we don't run into any issues
  ssh -q -F "${SSH_CONFIG_FILE}" "${FIRST_CLIENT_IP}" \
    "clush --hostfile=hosts_all --dsh 'rm -f ~/.ssh/known_hosts'"

  # Copy ~/.ssh directory to all instances
  ssh -q -F "${SSH_CONFIG_FILE}" "${FIRST_CLIENT_IP}" \
    "clush --hostfile=hosts_all --dsh --copy ~/.ssh --dest ~/"
}

configure_daos() {
  log "Configure DAOS instances"
  ssh -q -F "${SSH_CONFIG_FILE}" ${FIRST_CLIENT_IP} "~/configure_daos.sh"
}

show_instances() {
  log_section "DAOS Server and Client instances"
  DAOS_FILTER="$(echo ${DAOS_SERVER_BASE_NAME} | sed -r 's/server/.*/g')-.*"
  gcloud compute instances list \
    --project="${TF_VAR_project_id}" \
    --zones="${TF_VAR_zone}" \
    --filter="name~'^${DAOS_FILTER}'"
}

check_gvnic() {
  DAOS_SERVER_NETWORK_TYPE=$(ssh -q -F "${SSH_CONFIG_FILE}" ${FIRST_CLIENT_IP} "ssh ${DAOS_FIRST_SERVER} 'sudo lshw -class network'" | sed -n "s/^.*product: \(.*\$\)/\1/p")
  DAOS_CLIENT_NETWORK_TYPE=$(ssh -q -F "${SSH_CONFIG_FILE}" ${FIRST_CLIENT_IP} "sudo lshw -class network" | sed -n "s/^.*product: \(.*\$\)/\1/p")

  log_section "Network adapters type:"
  printf '%s\n%s\n' \
    "DAOS_SERVER_NETWORK_TYPE = ${DAOS_SERVER_NETWORK_TYPE}" \
    "DAOS_CLIENT_NETWORK_TYPE = ${DAOS_CLIENT_NETWORK_TYPE}"
}

show_run_steps() {

 log_section "DAOS Server and Client instances are ready for IO500 run"

 cat <<EOF

To run the IO500 benchmark:

1. Log into the first client
   ssh -F ./tmp/ssh_config ${FIRST_CLIENT_IP}

2. Run IO500
   ~/run_io500-sc21.sh

EOF
}

main() {
  check_dependencies
  opts "$@"
  create_active_config_symlink
  load_config
  create_hosts_files
  build_disk_images
  run_terraform
  configure_first_client_nat_ip
  configure_ssh
  copy_files_to_first_client
  propagate_ssh_keys_to_all_nodes
  show_instances
  check_gvnic
  show_run_steps
}

main "$@"

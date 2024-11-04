script_dir=/home/rjensen1/workspace/daos/utils
distro="${1:-rockylinux:9}"
distro_clean="${distro//[:\/]/_}"

rpm_dst=/tmp/rpms/$distro_clean

echo "Distro options: 'rockylinux:8', 'rockylinux:9', 'opensuse/leap:15.4', 'opensuse/leap:15.5']"

echo "Running docker with '$distro'. RPMs will be saved at '$rpm_dst'"

mkdir -p "$rpm_dst"
chmod 777 "$rpm_dst"
docker run -it --name "${distro_clean}_rpms" -v "$rpm_dst":/root/rpms -v "$script_dir":/root/bin $distro /bin/bash
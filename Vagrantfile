# vi: set ft=ruby :

require 'etc'

Vagrant.configure("2") do |config|

        # disable the default shared folder
        config.vm.synced_folder ".", "/vagrant", disabled: true
        # but share the user's daoshome
        config.vm.synced_folder ".", File.dirname(__FILE__), type: "nfs", 
                  nfs_version: 4,
                  nfs_udp: false
        config.nfs.map_uid = 1001
        config.nfs.map_gid = 1001

        # use the "images" storage pool
        # possibly first: virsh pool-start images
        # virsh pool-start images
        config.vm.provider :libvirt do |libvirt, override|
                override.vm.box = "centos/7"
                # set to distro version desired for test
                #override.vm.box_version = "> 1804, < 9999"
                #libvirt.storage_pool_name = "images"
                libvirt.memory = 1024 * 32
                libvirt.cpus = 2
        end

        # Increase yum timeout for slow mirrors
        config.vm.provision "extend yum mirror timeout",
                type: "shell",
                inline: "sed -i -e '/^distroverpkg/atimeout=300' /etc/yum.conf"

        # Vagrant (dumbly) adds the host's name to the loopback /etc/hosts
        # entry.  https://github.com/hashicorp/vagrant/issues/7263
        # They seem to be preferring simplicity-in-simple scenarios and
        # do really bad things to that end.  Things that blow up in real-
        # world complexity levels
        config.vm.provision "fix /etc/hosts",
                type: "shell",
                inline: "sed -i -e \"/^127.0.0.1/s/\$HOSTNAME//g\" -e '/^127.0.0.1[         ]*$/d' /etc/hosts"

        # Verbose booting
        config.vm.provision "fix grub", type: "shell",
                            inline: "sed -ie 's/ rhgb quiet//' /boot/grub2/grub.cfg /etc/sysconfig/grub"

        # The VMs will have IPv6 but no IPv6 connectivity so alter
        # their gai.conf to prefer IPv4 addresses over IPv6
        config.vm.provision "fix gai.conf", type: "shell",
                            inline: "echo \"precedence ::ffff:0:0/96  100\" > /etc/gai.conf
for i in all default; do
    echo 1 > /proc/sys/net/ipv6/conf/$i/disable_ipv6
done
if ! grep ip_resolve= /etc/yum.conf; then
    sed -i -e '/^\\[main\\]$/aip_resolve=4' /etc/yum.conf
fi"

        # install needed packages for daos
        config.vm.provision "install epel-release", type: "shell",
                            inline: "yum -y install epel-release"
        config.vm.provision "Install basic packages 1", \
                            type: "shell",              \
                            inline: "yum -y install librdmacm libcmocka ed \
                                     python-clustershell python3-pip strace"

        # A simple way to create a key that can be used to enable
        # SSH between the virtual guests.
        #
        # The private key is copied onto the root account of the
        # administration node and the public key is appended to the
        # authorized_keys file of the root account for all nodes
        # in the cluster.
        #
        # Shelling out may not be the most Vagrant-friendly means to
        # create this key but it avoids more complex methods such as
        # developing a plugin.
        #
        # Popen may be a more secure way to exec but is more code
        # for what is, in this case, a relatively small gain.
        if not(File.exist?("id_rsa"))
                #res = system("ssh-keygen -t rsa -N '' -f id_rsa -C \"Vagrant cluster\"")
                system("ssh-keygen -t rsa -N '' -f id_rsa -C \"Vagrant cluster\"")
        end

        # Add the generated SSH public key to each host's
        # authorized_keys file.
        config.vm.provision "copy id_rsa.pub", type: "file", source: "id_rsa.pub", destination: "/tmp/id_rsa.pub"
        config.vm.provision "fix authorized_keys", type: "shell", inline: "mkdir -m 0700 -p /root/.ssh
if [ -f /tmp/id_rsa.pub ]; then
    awk -v pk=\"$(cat /tmp/id_rsa.pub)\" 'BEGIN{split(pk,s,\" \")} $2 == s[2] {m=1;exit}END{if (m==0)print pk}' /root/.ssh/authorized_keys >> /root/.ssh/authorized_keys
    awk -v pk=\"$(cat /tmp/id_rsa.pub)\" 'BEGIN{split(pk,s,\" \")} $2 == s[2] {m=1;exit}END{if (m==0)print pk}' /home/vagrant/.ssh/authorized_keys >> /home/vagrant/.ssh/authorized_keys
fi
cat /home/vagrant/.ssh/authorized_keys >> /root/.ssh/authorized_keys
chmod 0600 /root/.ssh/authorized_keys"

        # And make the private key available
        config.vm.provision "copy id_rsa", type: "file", source: "id_rsa", destination: "/tmp/id_rsa"
        config.vm.provision "configure ssh", type: "shell", inline: "mkdir -m 0700 -p /root/.ssh
cp /tmp/id_rsa /home/vagrant/.ssh/.
cat <<EOF > /home/vagrant/.ssh/config 
Host vm*
  #UserKnownHostsFile /dev/null
  StrictHostKeyChecking no
EOF
chown vagrant.vagrant /home/vagrant/.ssh/{id_rsa,config}
chmod 600 /home/vagrant/.ssh/config
cp /tmp/id_rsa /root/.ssh/.
chmod 0600 /root/.ssh/id_rsa"
        #
        # Create the cluster
        #
        (1..9).each do |ss_idx|
                config.vm.define "vm#{ss_idx}", autostart: true do |ss|
                        ss.vm.host_name = "vm#{ss_idx}"
                        ss.vm.provider :libvirt do |lv|
                                # An NVMe drive:
                                lv.qemuargs :value => "-drive"
                                lv.qemuargs :value => "format=raw,file=/home/brian/.local/share/libvirt/images/nvme_disk#{ss_idx}.img,if=none,id=NVME#{ss_idx}"
                                lv.qemuargs :value => "-device"
                                lv.qemuargs :value => "nvme,drive=NVME#{ss_idx},serial=nvme-#{ss_idx}"
                                # PMEM
                                lv.qemuargs :value => "-machine"
                                lv.qemuargs :value => "pc,accel=kvm,nvdimm=on"
                                lv.qemuargs :value => "-m"
                                lv.qemuargs :value => "8G,slots=2,maxmem=40G"
                                lv.qemuargs :value => "-object"
                                lv.qemuargs :value => "memory-backend-file,id=mem#{ss_idx},share=on,mem-path=/home/brian/tmp/nvdimm#{ss_idx},size=32768M"
                                lv.qemuargs :value => "-device"
                                lv.qemuargs :value => "nvdimm,id=nvdimm#{ss_idx},memdev=mem#{ss_idx},label-size=2097152"
                        end
                        config.vm.provision "Configure selinux", type: "shell", inline: "selinuxenabled && setenforce 0; cat >/etc/selinux/config<<__EOF
SELINUX=disabled
SELINUXTYPE=targeted
__EOF"
                        config.vm.provision "Allow ssh passwords", type: "shell", inline: "sed -i -e '/PasswordAuthentication no/s/no/yes/' /etc/ssh/sshd_config"
                        config.vm.provision "Install basic tools", type: "shell", inline: "yum -y install time"
                end
        end
#        config.vm.define "vm1", autostart: true do |c|
#                # not sure why I was doing this -- don't land this
#                #c.vm.host_name = "vm9"
#                ss_idx = 1
#                c.vm.host_name = "vm#{ss_idx}"
#                c.vm.provider :libvirt do |lv|
#                        # An NVMe drive:
#                        lv.qemuargs :value => "-drive"
#                        lv.qemuargs :value => "file=/home/brian/.local/share/libvirt/images/nvme_disk1.img,if=none,id=NVME1"
#                        lv.qemuargs :value => "-device"
#                        lv.qemuargs :value => "nvme,drive=NVME1,serial=nvme-1"
#                        # PMEM
#                        lv.qemuargs :value => "-machine"
#                        lv.qemuargs :value => "pc,accel=kvm,nvdimm=on"
#                        lv.qemuargs :value => "-m"
#                        lv.qemuargs :value => "2G,slots=2,maxmem=34G"
#                        lv.qemuargs :value => "-object"
#                        lv.qemuargs :value => "memory-backend-file,id=mem1,share=on,mem-path=/home/brian/tmp/nvdimm1,size=32768M"
#                        lv.qemuargs :value => "-device"
#                        lv.qemuargs :value => "nvdimm,id=nvdimm1,memdev=mem1,label-size=2097152"
#                end
#                config.vm.provision "Configure selinux", type: "shell", inline: "selinuxenabled && setenforce 0; cat >/etc/selinux/config<<__EOF
#SELINUX=disabled
#SELINUXTYPE=targ
#__EOF"
#                config.vm.provision "Allow ssh passwords", type: "shell", inline: "sed -i -e '/PasswordAuthentication no/s/no/yes/' /etc/ssh/sshd_config"
#                config.vm.provision "Install basic tools", type: "shell", inline: "yum -y install time"
#        end
end

#system("[ -f vagrant_ssh_config ] || vagrant ssh-config > vagrant_ssh_config")

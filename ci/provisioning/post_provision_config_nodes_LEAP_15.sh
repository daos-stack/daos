#!/bin/bash

post_provision_config_nodes() {
    # TODO: port this to Zypper
    #       or do we even need it any more?
    #if $CONFIG_POWER_ONLY; then
    #    rm -f /etc/yum.repos.d/*.hpdd.intel.com_job_daos-stack_job_*_job_*.repo
    #    yum -y erase fio fuse ior-hpc mpich-autoload               \
    #                 ompi argobots cart daos daos-client dpdk      \
    #                 fuse-libs libisa-l libpmemobj mercury mpich   \
    #                 openpa pmix protobuf-c spdk libfabric libpmem \
    #                 libpmemblk munge-libs munge slurm             \
    #                 slurm-example-configs slurmctld slurm-slurmmd
    #fi
    
    # Temp fix for broken mirror until snapshot is rebuilt to not use it.
    zypper --non-interactive mr -d openSUSE-Leap-15.1-1 || true 
    zypper --non-interactive mr -d openSUSE-Leap-15.1-Non-Oss || true
    zypper --non-interactive mr -d openSUSE-Leap-15.1-Oss || true
    zypper --non-interactive mr -d openSUSE-Leap-15.1-Update || true
    zypper --non-interactive mr -d openSUSE-Leap-15.1-Update-Non-Oss || true
    zypper --non-interactive rm fuse

    echo 'solver.allowVendorChange = true' >> /etc/zypp/zypp.conf
    # because our Nexus is broken
    zypper --non-interactive ar \
           'https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_15.1/science:HPC.repo'
    zypper --non-interactive ar --gpgcheck-allow-unsigned \
           'https://build.hpdd.intel.com/job/daos-stack/job/python-avocado/job/PR-1/lastSuccessfulBuild/artifact/artifacts/leap15/' avocado
    # disable troublesome repos
    # Repository 'openSUSE-Leap-15.1-Update' is invalid.
    # [openSUSE-Leap-15.1-Update|https://mirrors.kernel.org/opensuse/update/leap/15.1/oss/] Valid metadata not found at specified URL
    # History:
    #  - File './repodata/a1ec055b3dd037afa3008dc30eb9e63834cd9602d25de8cfc1b3446ae6639973-deltainfo.xml.gz' not found on medium 'https://mirrors.kernel.org/opensuse/update/leap/15.1/oss/'
    #  - Can't provide ./repodata/a1ec055b3dd037afa3008dc30eb9e63834cd9602d25de8cfc1b3446ae6639973-deltainfo.xml.gz
    zypper --non-interactive mr -d 'openSUSE-Leap-15.1-Update'
    # to get a python2-lzma that isn't broken
    zypper --non-interactive ar http://download.opensuse.org/distribution/leap/15.2/repo/oss/ 15.2_oss
    zypper --non-interactive --gpg-auto-import-keys ref avocado 15.2_oss 'All packages used mainly in HPC (openSUSE_Leap_15.1)'
    # remove to avoid conflicts
    zypper --non-interactive rm python2-Fabric Modules
    zypper --non-interactive in python2-avocado-plugins-varianter-yaml-to-mux \
                                python2-avocado-plugins-output-html           \
                                patch python2-Jinja2 pciutils lua-lmod
    zypper --non-interactive rr 15.2_oss
    rpm -qa | grep kernel
    
    if [ -n "$DAOS_STACK_GROUP_REPO" ]; then
         # rm -f /etc/yum.repos.d/*"$DAOS_STACK_GROUP_REPO"
        zypper --non-interactive ar "$REPOSITORY_URL"/"$DAOS_STACK_GROUP_REPO" daos-stack-group-repo
        zypper --non-interactive mr --gpgcheck-allow-unsigned-repo daos-stack-group-repo
        # Group repo currently needs this key.
        rpm --import 'https://download.opensuse.org/repositories/science:/HPC/openSUSE_Leap_15.1/repodata/repomd.xml.key' ||
        rpm --import 'https://provo-mirror.opensuse.org/repositories/science:/HPC/openSUSE_Leap_15.1/repodata/repomd.xml.key'
    fi
    
    if [ -n "$DAOS_STACK_LOCAL_REPO" ]; then
        zypper --non-interactive ar --gpgcheck-allow-unsigned "$REPOSITORY_URL"/"$DAOS_STACK_LOCAL_REPO" daos-stack-local-repo
        zypper --non-interactive mr --no-gpgcheck daos-stack-local-repo
    fi
    
    if [ -n "$INST_REPOS" ]; then
        for repo in $INST_REPOS; do
            branch="master"
            build_number="lastSuccessfulBuild"
            if [[ $repo = *@* ]]; then
                branch="${repo#*@}"
                repo="${repo%@*}"
                if [[ $branch = *:* ]]; then
                    build_number="${branch#*:}"
                    branch="${branch%:*}"
                fi
            fi
            zypper --non-interactive ar --gpgcheck-allow-unsigned "${JENKINS_URL}"job/daos-stack/job/"${repo}"/job/"${branch//\//%252F}"/"${build_number}"/artifact/artifacts/leap15/ "$repo"
        done
    fi
    # some hackery due to broken repos
    x=0
    while [ $x -lt 5 ]; do
        if time zypper --non-interactive         \
                       --gpg-auto-import-keys    \
                       --no-gpg-checks ref; then
            break
        fi
        ((x++)) || true
        if [ $x -eq 3 ]; then
            zypper --non-interactive rr openSUSE-Leap-15.1-Update-Non-Oss
            zypper --non-interactive rr openSUSE-Leap-15.1-Non-Oss
        fi
    done
    #if [ -n "$INST_RPMS" ]; then
        #yum -y erase $INST_RPMS
    #fi
    if ! zypper --non-interactive in ed nfs-client ipmctl ndctl sudo \
                                     nfs-kernel-server               \
                                     $INST_RPMS; then
        rc=${PIPESTATUS[0]}
        for file in /etc/zypp/repos.d/*.repo; do
            echo "---- $file ----"
            cat "$file"
        done
        exit "$rc"
    fi
}

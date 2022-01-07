#!/bin/sh -e

# For fedora, java-11 is installed along with maven if we install maven from
# repo. But we need java-8 (1.8). The 'devel' package also needs to be
# installed specifically.
# For centos, we can install both java-8 and maven from repo.

if [ -e /etc/fedora-release ]; then
	dnf install java-1.8.0-openjdk java-1.8.0-openjdk-devel maven-openjdk8
else
        dnf install java-1.8.0-openjdk maven
fi

dnf clean all

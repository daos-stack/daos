#!/bin/sh

# For fedora, java-11 is installed along with maven if we install maven from
# repo. But we need java-8 (1.8). The 'devel' package also needs to be
# installed specifically.
# For centos, we can install both java-8 and maven from repo.

set -e

fedora=$1

if "$fedora"; then
	dnf install java-1.8.0-openjdk java-1.8.0-openjdk-devel
        curl --output ./apache-maven-3.6.3-bin.tar.gz \
https://archive.apache.org/dist/maven/maven-3/3.6.3/binaries/apache-maven-3.6.3-bin.tar.gz \
&& tar -xf apache-maven-3.6.3-bin.tar.gz \
&& find ./ -name "mvn" | \
{ read -r d; f="$(dirname "$d")"; cp -r "$(readlink -f "$f")"/../* /usr/local; }

else
        dnf install java-1.8.0-openjdk maven
fi

dnf clean all

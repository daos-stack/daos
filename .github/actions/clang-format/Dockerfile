FROM fedora:latest

RUN dnf -y install clang-tools-extra git-clang-format

COPY entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]

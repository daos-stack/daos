FROM fedora:38

RUN dnf -y install clang-tools-extra git-clang-format

COPY entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]

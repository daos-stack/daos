FROM fedora:38

RUN dnf -y install clang-tools-extra

RUN clang-format --version

COPY entrypoint.sh /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]

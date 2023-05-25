FROM fedora:38

RUN dnf -y install clang-tools-extra

RUN clang-format --version

RUN ls

COPY . .

RUN ls

ENTRYPOINT ["/entrypoint.sh"]

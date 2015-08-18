#!/bin/sh

#autoreconf --force --install -I config -I m4
exec autoreconf -v --force --install -I build

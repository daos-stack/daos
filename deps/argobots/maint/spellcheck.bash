#! /usr/bin/env bash
##
## Copyright (C) by Argonne National Laboratory
##     See COPYRIGHT in top-level directory
##

IGNORE_WORDS="inout,cas,ans,numer"
SKIP_FILES=".git,*.tex,*.bib,*.sty,*.f,confdb/config.*,*.png,*.rpath,*.m4"
codespell --ignore-words-list="${IGNORE_WORDS}" --skip="${SKIP_FILES}"

# Git hooks

This directory contains githooks that can be employed locally to
automate things like copyright updates on modified files

To use the pre-commit hook here, do the following locally

```

cat << EOF 2>&1 > .git/hooks/pre-commit
#!/bin/sh

# Invoke the githook for copyright validation
exec utils/githooks/pre-commit 1>&2
EOF

chmod 700 .git/hooks/pre-commit

```

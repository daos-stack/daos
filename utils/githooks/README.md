# Git hooks

This directory contains a githooks framework that can be
employed locally to automate things like copyright updates
on modified files.

It supports adding local hooks by placing your custom hook in
utils/githooks/<hook>.d/<num>-<name> files.

To use the pre-commit hook here, do the following locally

```
ln -s ../../utils/githooks/pre-commit .git/hooks/pre-commit
```

# Git hooks

This directory contains a githooks framework that can be employed locally to automate things like
copyright updates on modified files.

It supports adding local hooks by placing your custom hook in
`utils/githooks/<hook>.d/<num>-user-<name>` files.

To use the commit hooks here, do the following locally, or alternatively copy the files into place.

```sh
for d in utils/githooks/*.d; do
    h=${d%.d}
    h=${h##*/}
    ln -s ../../utils/githooks/$h .git/hooks/$h
done
```

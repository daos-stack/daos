# Git hooks

This directory contains a githooks framework that can be
employed locally to automate things like copyright updates
on modified files.

It supports adding local hooks by placing your custom hook in
utils/githooks/<hook>.d/<num>-<name> files.

To use the pre-commit hook here, do the following locally

```
for d in utils/githooks/*.d; do
    h=${d%.d};
    h=${h##*/}; echo $h; done
    ln -s ../../utils/githooks/$h .git/hooks/$h
done
```


### Pylint tool for checking code quality.

Daos contains a wrapper program for pylint to automatically apply appropriate settings to different
parts of the source tree.  This program can be used as a drop-in replacement for pylint
in some places although also supports other modes, for example as a git commit hook.

## Visual Studio Code

To use daos_pylint.py in VS Code apply the following settings to settings.json, using the full
path to your source tree.

```
{
    "pylint.importStrategy": "fromEnvironment",
    "python.linting.pylintArgs": [
        "--promote-to-error"
    ],
    "pylint.path": [
        "<checkout_root>/utils/cq/daos_pylint.py"
    ]
}
```

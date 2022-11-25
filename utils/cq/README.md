
### Pylint tool for checking code quality.

Daos contains a wrapper program for pylint to automatically apply appropriate settings to different
parts of the source tree.  This program can be used as a drop-in replacement for pylint
in some places although also supports other options.

## Visual Studio Code

To use daos_pylint.py in VS Code apply the following settings to settings.json

```
{
    "pylint.importStrategy": "fromEnvironment",
    "python.linting.pylintArgs": [
        "--import={workspace}/venv/lib/python3.9/site-packages"
    ],
    "pylint.path": [
        "{workspace}/utils/cq/daos_pylint.py"
    ]
}
```

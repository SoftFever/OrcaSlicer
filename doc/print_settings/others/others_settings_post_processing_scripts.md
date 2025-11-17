# Post-Processing Scripts

Here you can set up post-processing scripts that will be executed after slicing.  
This allows you to modify the G-code output or perform additional tasks.
Placeholder tokens are supported and will be replaced before scripts are executed.

Check the script's documentation for dependencies, available parameters and usage instructions.

Example Python script:

```shell
"C:\Your\Path\To\Python\python.exe" "C:\Your\Path\To\Script\pythonScript.py" -parameterToScript 1994 -layerHeight {layer_height};
```

> [!TIP]
> Check [Built in placeholders variables](built-in-placeholders-variables) for available tokens and their meanings.
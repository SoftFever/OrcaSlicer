# How to validate profiles

This guide describes how to validate profile configurations for [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) using the `OrcaSlicer_profile_validator` tool.

## Download

You can download the validator executable from the official repository:  
[https://github.com/SoftFever/Orca_tools](https://github.com/SoftFever/Orca_tools)

- Use `OrcaSlicer_profile_validator` for Ubuntu.
- Use `OrcaSlicer_profile_validator.exe` for Windows.

## Usage

```
-h [ --help ]                   Show help message
-p [ --path ] arg               Profile folder path (required)
-v [ --vendor ] arg             Vendor name (optional). If not specified, all profiles in the folder will be validated.
-l [ --log_level ] arg (=2)     Log level (optional). Default is 2 (warning). Higher values increase verbosity.
```

## Example

```
./OrcaSlicer_profile_validator -p ~/codes/OrcaSlicer/resources/profiles -l 2 -v Custom
```

## Sample Output

### When Errors Are Found

```
[2024-02-28 21:23:06.102138] [0x0000a4e8] [error]   Slic3r::ConfigBase::load_from_json: parse d:\codes\OrcaSlicer\resources\profiles/Custom/machine/fdm_klipper_common.json got a nlohmann::detail::parse_error, reason = [json.exception.parse_error.101] parse error at line 9, column 38: syntax error while parsing object - unexpected string literal; expected '}'
[2024-02-28 21:23:06.102638] [0x0000a4e8] [error]   Slic3r::PresetBundle::load_vendor_configs_from_json::<lambda>::operator (): load config file d:\codes\OrcaSlicer\resources\profiles/Custom/machine/fdm_klipper_common.json Failed!
[2024-02-28 21:23:06.102638] [0x0000a4e8] [error]   Slic3r::PresetBundle::load_vendor_configs_from_json, got error when parse printer setting from d:\codes\OrcaSlicer\resources\profiles/Custom/machine/fdm_klipper_common.json
[2024-02-28 21:23:06.103138] [0x0000a4e8] [error]   Failed loading configuration file d:\codes\OrcaSlicer\resources\profiles/Custom/machine/fdm_klipper_common.json
Suggest cleaning the directory d:\codes\OrcaSlicer\resources\profiles first
Validation failed
```

### When Validation Is Successful

```
Validation completed successfully
```

## Notes

- Ensure that the profile JSON files are correctly formatted.
- A clean directory structure improves validation reliability.
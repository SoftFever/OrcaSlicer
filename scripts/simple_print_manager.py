# Simple 3D print job manager for OrcaSlicer
# This script guides the user through slicing a model using OrcaSlicer CLI.
# It explains each step and tries to be user friendly.

import os
import shlex
import subprocess
from pathlib import Path


def ask_path(prompt: str, must_exist: bool = True, is_dir: bool = False) -> str:
    """Ask the user for a file or directory path and validate it."""
    while True:
        path = input(prompt).strip().strip('"')
        if not path:
            print("Please provide a path.")
            continue
        p = Path(path)
        if must_exist and not p.exists():
            print(f"The path '{path}' does not exist. Please try again.")
            continue
        if is_dir and not p.is_dir():
            print(f"The path '{path}' is not a directory. Please try again.")
            continue
        if not is_dir and p.is_dir():
            print(f"The path '{path}' is a directory. Please provide a file path.")
            continue
        return str(p)


print("Welcome to the OrcaSlicer Simple Print Manager!")

# 1. Ask for OrcaSlicer CLI path
cli_path = ask_path(
    "Where is the OrcaSlicer command-line tool (orcaslicer-cli) located? "
    "(e.g., C:/Program Files/OrcaSlicer/orcaslicer-cli.exe)\n",
    must_exist=True,
)

# 2. Ask for model file
model_path = ask_path(
    "What is the full path to your 3D model file (e.g., .stl, .3mf)?\n",
    must_exist=True,
)

# 3. Ask for print intent
intent = input(
    "What kind of print do you want? Type one of the following: DRAFT, QUALITY, or STRONG\n"
).strip().upper()

profiles = {
    "DRAFT": "draft_profile.ini",
    "QUALITY": "quality_profile.ini",
    "STRONG": "strong_profile.ini",
}
profile_name = profiles.get(intent)
if profile_name is None:
    print("Unknown print type. Please run the script again and choose from DRAFT, QUALITY, or STRONG.")
    raise SystemExit(1)

# 4. Ask for profile folder
profile_folder = ask_path(
    "Where is the folder containing your OrcaSlicer profiles?\n",
    must_exist=True,
    is_dir=True,
)
profile_path = os.path.join(profile_folder, profile_name)
if not os.path.exists(profile_path):
    print(f"Warning: The profile '{profile_name}' was not found in {profile_folder}.")

# 5. Construct output path next to model file
model = Path(model_path)
output_path = str(model.with_name(model.stem + "_sliced.gcode"))

# Build the command to run OrcaSlicer
command = [
    cli_path,
    "--slice",
    model_path,
    "--output",
    output_path,
    "--load",
    profile_path,
]
command_str = " ".join(shlex.quote(arg) for arg in command)
print("I'm about to run:")
print(command_str)

proceed = input("Do you want to proceed? (yes/no)\n").strip().lower()
if proceed not in {"yes", "y"}:
    print("Operation cancelled by user.")
    raise SystemExit(0)

# Run the command
try:
    result = subprocess.run(command, check=True)
    print(f"Slicing complete! Your G-code file should be at: {output_path}")
except (subprocess.CalledProcessError, FileNotFoundError):
    print(
        "Slicing failed. OrcaSlicer might have reported an error, or the paths might be incorrect."
    )

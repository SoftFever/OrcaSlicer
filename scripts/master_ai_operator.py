"""Master AI Operator for OrcaSlicer.

This script guides a user through analyzing a model, selecting
an appropriate print intent and slicing the model using the
OrcaSlicer command line interface.

Requirements:
    - Python 3.8+
    - The `trimesh` package for 3D model analysis. Install with:
      `pip install trimesh`

The goal is to be beginner friendly. Each major step is explained
and inputs are validated.
"""

from __future__ import annotations

import os
import shlex
import subprocess
from pathlib import Path

try:
    import trimesh
except ImportError:  # pragma: no cover - trivial import guard
    print("This script requires the 'trimesh' package. Install it with 'pip install trimesh'.")
    raise


def ask_path(prompt: str, must_exist: bool = True, is_dir: bool = False) -> str:
    """Ask the user for a path and validate it."""
    while True:
        path_str = input(prompt).strip().strip('"')
        if not path_str:
            print("Please provide a path.")
            continue
        path = Path(path_str)
        if must_exist and not path.exists():
            print(f"The path '{path}' does not exist. Please try again.")
            continue
        if is_dir and not path.is_dir():
            print(f"The path '{path}' is not a directory. Please try again.")
            continue
        if not is_dir and path.is_dir():
            print(f"The path '{path}' is a directory. Please provide a file path.")
            continue
        return str(path)


def analyze_model(model_file: str) -> tuple[int, float, float]:
    """Load the model with trimesh and return triangle count, max dimension and volume."""
    mesh = trimesh.load(model_file)
    triangles = int(mesh.faces.shape[0])
    bounds = mesh.bounds
    dims = bounds[1] - bounds[0]
    max_dimension = float(dims.max())
    volume_cm3 = float(mesh.volume) / 1000.0  # convert from mm^3 to cm^3
    print(
        f"Model Analysis: {triangles} triangles, Max Dimension: {max_dimension:.2f}mm, "
        f"Volume: {volume_cm3:.2f}cm\N{SUPERSCRIPT THREE}."
    )
    return triangles, max_dimension, volume_cm3


def determine_intent(max_dim: float, triangles: int, purpose: str) -> str:
    """Determine print intent based on size, complexity and purpose."""
    purpose = purpose.upper()
    if purpose == "FUNCTIONAL" and (max_dim > 100 or triangles > 200_000):
        return "STRONG"
    if purpose == "FUNCTIONAL":
        return "QUALITY"
    if purpose == "VISUAL" and (max_dim < 50 or triangles < 100_000):
        return "QUALITY"
    if purpose == "VISUAL":
        return "DRAFT"
    print("Unknown purpose provided, defaulting intent to QUALITY.")
    return "QUALITY"


def run_cli(cli: str, model: str, profile: str, output: str) -> bool:
    """Execute OrcaSlicer CLI and return True if it appears successful."""
    command = [cli, "--slice", model, "--output", output, "--load", profile]
    command_str = " ".join(shlex.quote(arg) for arg in command)
    print(f"Executing: {command_str}")
    try:
        subprocess.run(command, check=True)
        return True
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False


def main() -> None:
    print("Welcome to the Master AI Operator for OrcaSlicer!")

    cli_path = ask_path(
        "Path to OrcaSlicer CLI (e.g., C:/Path/To/orcaslicer-cli.exe):\n"
    )
    profile_folder = ask_path(
        "Path to your OrcaSlicer profiles folder (e.g., C:/Path/To/Profiles/):\n",
        is_dir=True,
    )
    model_file = ask_path(
        "Full path to your 3D model file (e.g., .stl, .3mf):\n"
    )

    triangles, max_dim, volume = analyze_model(model_file)

    purpose = input(
        "Is this model primarily for FUNCTIONAL or VISUAL use? (Type FUNCTIONAL or VISUAL):\n"
    ).strip().upper()

    intent = determine_intent(max_dim, triangles, purpose)
    print(f"AI has selected intent: {intent}")

    profiles = {
        "DRAFT": "draft_profile.ini",
        "QUALITY": "quality_profile.ini",
        "STRONG": "strong_profile.ini",
    }
    profile_path = os.path.join(profile_folder, profiles[intent])

    model = Path(model_file)
    output_path = str(model.with_name(model.stem + "_AI_sliced.gcode"))

    success = run_cli(cli_path, model_file, profile_path, output_path)

    if success:
        print(f"Slicing complete! G-code saved to: {output_path}")
    else:
        print("Slicing attempt encountered an issue. Please check paths and OrcaSlicer CLI output if any.")


if __name__ == "__main__":
    main()

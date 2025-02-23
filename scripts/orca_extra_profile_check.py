import os
import json
import argparse
from pathlib import Path

# Add helper function for duplicate key detection.
def no_duplicates_object_pairs_hook(pairs):
    seen = {}
    for key, value in pairs:
        if key in seen:
            raise ValueError(f"Duplicate key detected: {key}")
        seen[key] = value
    return seen

def check_filament_compatible_printers(vendor_folder):
    """
    Checks JSON files in the vendor folder for missing or empty 'compatible_printers'
    when 'instantiation' is flagged as true.

    Parameters:
        vendor_folder (str or Path): The directory to search for JSON profile files.

    Returns:
        int: The number of profiles with missing or empty 'compatible_printers'.
    """
    error = 0
    vendor_path = Path(vendor_folder)
    if not vendor_path.exists():
        return 0
    # Use rglob to recursively find .json files.
    for file_path in vendor_path.rglob("*.json"):
        try:
            with open(file_path, 'r') as fp:
                # Use custom hook to detect duplicates.
                data = json.load(fp, object_pairs_hook=no_duplicates_object_pairs_hook)
        except ValueError as ve:
            print(f"Duplicate key error in {file_path}: {ve}")
            error += 1
            continue
        except Exception as e:
            print(f"Error processing {file_path}: {e}")
            error += 1
            continue

        instantiation = str(data.get("instantiation", "")).lower() == "true"
        compatible_printers = data.get("compatible_printers")
        if instantiation and (not compatible_printers or (isinstance(compatible_printers, list) and not compatible_printers)):
            print(file_path)
            error += 1
    return error

def main():
    print("Checking compatible_printers ...")
    parser = argparse.ArgumentParser(description="Check compatible_printers")
    parser.add_argument("--vendor", type=str, required=False, help="Vendor name")
    args = parser.parse_args()
    
    script_dir = Path(__file__).resolve().parent
    profiles_dir = script_dir.parent / "resources" / "profiles"
    checked_vendor_count = 0
    errors_found = 0
    
    if args.vendor:
        errors_found += check_filament_compatible_printers(profiles_dir / args.vendor / "filament")
        checked_vendor_count += 1
    else:
        for vendor_dir in profiles_dir.iterdir():
            # skip "OrcaFilamentLibrary" folder
            if vendor_dir.name == "OrcaFilamentLibrary":
                continue
            if vendor_dir.is_dir():
                errors_found += check_filament_compatible_printers(vendor_dir / "filament")
            checked_vendor_count += 1

    if errors_found > 0:
        print(f"Errors found in {errors_found} profile files")
        exit(-1)
    else:
        print(f"Checked {checked_vendor_count} vendor files")
        exit(0)


if __name__ == "__main__":
    main()

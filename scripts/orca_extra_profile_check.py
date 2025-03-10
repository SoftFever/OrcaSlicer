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

def load_available_filament_profiles(profiles_dir, vendor_name):
    """
    Load all available filament profiles from a vendor's directory.
    
    Parameters:
        profiles_dir (Path): The directory containing vendor profile directories
        vendor_name (str): The name of the vendor directory
        
    Returns:
        set: A set of filament profile names
    """
    profiles = set()
    vendor_path = profiles_dir / vendor_name / "filament"
    
    if not vendor_path.exists():
        return profiles
    
    for file_path in vendor_path.rglob("*.json"):
        try:
            with open(file_path, 'r') as fp:
                data = json.load(fp)
                if "name" in data:
                    profiles.add(data["name"])
        except Exception as e:
            print(f"Error loading filament profile {file_path}: {e}")
    
    return profiles

def check_machine_default_materials(profiles_dir, vendor_name):
    """
    Checks if default materials referenced in machine profiles exist in 
    the vendor's filament library or in the global OrcaFilamentLibrary.
    
    Parameters:
        profiles_dir (Path): The base profiles directory
        vendor_name (str): The vendor name to check
        
    Returns:
        int: Number of missing filament references found
    """
    error_count = 0
    machine_dir = profiles_dir / vendor_name / "machine"
    
    if not machine_dir.exists():
        print(f"No machine profiles found for vendor: {vendor_name}")
        return 0
        
    # Load available filament profiles
    vendor_filaments = load_available_filament_profiles(profiles_dir, vendor_name)
    global_filaments = load_available_filament_profiles(profiles_dir, "OrcaFilamentLibrary")
    all_available_filaments = vendor_filaments.union(global_filaments)
    
    # Check each machine profile
    for file_path in machine_dir.rglob("*.json"):
        try:
            with open(file_path, 'r') as fp:
                data = json.load(fp)
                
            default_materials = None
            if "default_materials" in data:
                default_materials = data["default_materials"]
            elif "default_filament_profile" in data:
                default_materials = data["default_filament_profile"]
                
            if default_materials:
                if isinstance(default_materials, list):
                    for material in default_materials:
                        if material not in all_available_filaments:
                            print(f"Missing filament profile: '{material}' referenced in {file_path.relative_to(profiles_dir)}")
                            error_count += 1
                else:
                    # Handle semicolon-separated list of materials in a string
                    if ";" in default_materials:
                        for material in default_materials.split(";"):
                            material = material.strip()
                            if material and material not in all_available_filaments:
                                print(f"Missing filament profile: '{material}' referenced in {file_path.relative_to(profiles_dir)}")
                                error_count += 1
                    else:
                        # Single material in a string
                        if default_materials not in all_available_filaments:
                            print(f"Missing filament profile: '{default_materials}' referenced in {file_path.relative_to(profiles_dir)}")
                            error_count += 1
                        
        except Exception as e:
            print(f"Error processing machine profile {file_path}: {e}")
            error_count += 1
            
    return error_count

def main():
    print("Checking compatible_printers ...")
    parser = argparse.ArgumentParser(description="Check profiles for issues")
    parser.add_argument("--vendor", type=str, required=False, help="Vendor name")
    parser.add_argument("--check-filaments", default=True, action="store_true", help="Check compatible_printers in filament profiles")
    parser.add_argument("--check-materials", action="store_true", help="Check default materials in machine profiles")
    args = parser.parse_args()
    
    script_dir = Path(__file__).resolve().parent
    profiles_dir = script_dir.parent / "resources" / "profiles"
    checked_vendor_count = 0
    errors_found = 0
    
    if args.vendor:
        if args.check_filaments or not (args.check_materials and not args.check_filaments):
            errors_found += check_filament_compatible_printers(profiles_dir / args.vendor / "filament")
        if args.check_materials:
            errors_found += check_machine_default_materials(profiles_dir, args.vendor)
        checked_vendor_count += 1
    else:
        for vendor_dir in profiles_dir.iterdir():
            # skip "OrcaFilamentLibrary" folder
            if vendor_dir.name == "OrcaFilamentLibrary":
                continue
            if vendor_dir.is_dir():
                if args.check_filaments or not (args.check_materials and not args.check_filaments):
                    errors_found += check_filament_compatible_printers(vendor_dir / "filament")
                if args.check_materials:
                    errors_found += check_machine_default_materials(profiles_dir, vendor_dir.name)
            checked_vendor_count += 1

    if errors_found > 0:
        print(f"Errors found in {errors_found} profile files")
        exit(-1)
    else:
        print(f"Checked {checked_vendor_count} vendor files")
        exit(0)


if __name__ == "__main__":
    main()

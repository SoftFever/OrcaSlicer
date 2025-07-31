import os
import json
import argparse
from collections import defaultdict

def topological_sort(filaments):
    # Build a graph of dependencies
    graph = defaultdict(list)
    in_degree = defaultdict(int)
    name_to_filament = {f['name']: f for f in filaments}
    all_names = set(name_to_filament.keys())
    
    # Create the dependency graph
    processed_files = set()
    for filament in filaments:
        if 'inherits' in filament:
            parent = filament['inherits']
            child = filament['name']
            # Only create dependency if parent exists
            if parent in all_names:
                graph[parent].append(child)
                in_degree[child] += 1
                if parent not in in_degree:
                    in_degree[parent] = 0
                processed_files.add(child)
                processed_files.add(parent)

    # Initialize queue with nodes having no dependencies (now sorted)
    queue = sorted([name for name, degree in in_degree.items() if degree == 0])
    result = []

    # Process the queue
    while queue:
        current = queue.pop(0)
        result.append(name_to_filament[current])
        processed_files.add(current)
        
        # Process children (now sorted)
        children = sorted(graph[current])
        for child in children:
            in_degree[child] -= 1
            if in_degree[child] == 0:
                queue.append(child)

    # Add remaining files that weren't part of inheritance tree (now sorted)
    remaining = sorted(all_names - processed_files)
    for name in remaining:
        result.append(name_to_filament[name])

    return result

def update_profile_library(vendor="OrcaFilamentLibrary",profile_type="filament"):
    # change current working directory to the relative path(..\resources\profiles) compare to script location
    os.chdir(os.path.join(os.path.dirname(__file__), '..', 'resources', 'profiles'))

    # Collect current profile entries
    current_profiles = []
    base_dir = vendor
    profile_dir = os.path.join(base_dir, profile_type)
    
    for root, dirs, files in os.walk(profile_dir):
        for file in files:
            if file.lower().endswith('.json'):
                full_path = os.path.join(root, file)
                
                # Get relative path from base directory
                sub_path = os.path.relpath(full_path, base_dir).replace('\\', '/')
                
                try:
                    with open(full_path, 'r', encoding='utf-8') as f:
                        filament_data = json.load(f)
                        name = filament_data.get('name')
                        inherits = filament_data.get('inherits')
                        
                        if name:
                            entry = {
                                "name": name,
                                "sub_path": sub_path
                            }
                            if inherits:
                                entry['inherits'] = inherits
                            current_profiles.append(entry)
                        else:
                            print(f"Warning: Missing 'name' in {full_path}")
                except Exception as e:
                    print(f"Error reading {full_path}: {str(e)}")
                    continue

    # Sort profiles based on inheritance
    sorted_profiles = topological_sort(current_profiles)
    
    # Remove the inherits field as it's not needed in the final JSON
    for p in sorted_profiles:
        p.pop('inherits', None)

    # Update library file
    lib_path = f'{vendor}.json'

    profile_section = profile_type+'_list'
    
    try:
        with open(lib_path, 'r+', encoding='utf-8') as f:
            library = json.load(f)
            library[profile_section] = sorted_profiles
            f.seek(0)
            json.dump(library, f, indent=4, ensure_ascii=False)
            f.truncate()
            
        print(f"Profile library for {vendor} updated successfully!")
    except Exception as e:
        print(f"Error updating library file: {str(e)}")


# For each JSON file, it will:
#    - Replace "BBL X1C" with "System" in the name field
#    - Empty the compatible_printers array
#    - Ensure setting_id starts with 'O'
def rename_filament_system(vendor="OrcaFilamentLibrary"):
    # change current working directory to the relative path
    os.chdir(os.path.join(os.path.dirname(__file__), '..', 'resources', 'profiles'))
    
    base_dir = vendor
    filament_dir = os.path.join(base_dir, 'filament')
    
    for root, dirs, files in os.walk(filament_dir):
        for file in files:
            if file.lower().endswith('.json'):
                full_path = os.path.join(root, file)
                try:
                    with open(full_path, 'r', encoding='utf-8') as f:
                        data = json.load(f)
                        modified = False
                        
                        # Update name if it contains "BBL X1C"
                        if 'name' in data and "BBL X1C" in data['name']:
                            data['name'] = data['name'].replace("BBL X1C", "System")
                            modified = True
                            
                        # Empty compatible_printers if exists
                        if 'compatible_printers' in data:
                            data['compatible_printers'] = []
                            modified = True
                            
                        # Update setting_id if needed
                        if 'setting_id' in data and not data['setting_id'].startswith('O'):
                            data['setting_id'] = 'O' + data['setting_id']
                            modified = True
                        
                        if modified:
                            with open(full_path, 'w', encoding='utf-8') as f:
                                json.dump(data, f, indent=4, ensure_ascii=False)
                            print(f"Updated {full_path}")
                            
                except Exception as e:
                    print(f"Error processing {full_path}: {str(e)}")

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='Update filament library for specified vendor')
    parser.add_argument('-v', '--vendor', type=str, default="OrcaFilamentLibrary",
                      help='Vendor name (default: OrcaFilamentLibrary)')
    parser.add_argument('-m', '--mode', type=str, choices=['update', 'rename'],
                      default='update', help='Operation mode (default: update)')
    args = parser.parse_args()
    
    if args.mode == 'update':
        update_profile_library(args.vendor, 'filament')
        update_profile_library(args.vendor, 'process')
    else:
        rename_filament_system(args.vendor)
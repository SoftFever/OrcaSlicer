# Generates a vendor profile definition by parsing all of the specific profiles in the directory.
# Reduces the amount of manual copy/paste required to maintain all of the redundant duplicated information
# Usage: python3 update_profile.py VENDOR_NAME
# Example: python3 update_profile.py Anker

from argparse import ArgumentParser
from functools import cmp_to_key
import json
import locale
from pathlib import Path
import sys
from typing import Callable, Dict, List

VERBOSE = False

cli_args = ArgumentParser()
cli_args.add_argument('vendor', help='Vendor name. (Anker, BBL, etc)')
cli_args.add_argument('--verbose', '-v', action="store_true", default=False)

args = cli_args.parse_args()

VERBOSE = args.verbose

script_path = Path(__file__).resolve().parent
profile_path = script_path / args.vendor
json_path = script_path / f'{args.vendor}.json'

if not (profile_path.exists() and profile_path.is_dir()):
    print(f'Could not find vendor profile path: {profile_path}')
    sys.exit(-1)
if not (json_path.exists() and json_path.is_file()):
    print(f'Could not find vendor profile definition: {json_path}')
    sys.exit(-1)

if json_path.exists():
    print(f'found: {json_path}')
else:
    sys.exit()

anker_data = json.loads(json_path.read_text())


machine_model_key = 'machine_model_list'
machine_key = 'machine_list'
process_key = 'process_list'
filament_key = 'filament_list'

machine_path = profile_path / 'machine'
process_path = profile_path / 'process'
filament_path = profile_path / 'filament'

new_models = []
new_machines = []
new_processes = []
new_filaments = []

found_machines = {}

machine_paths = []

missing_deps = False

def anker_name_cmp(left: str, right: str) -> int:
    if left.startswith('fdm'):
        if right.startswith('fdm'):
            return locale.strcoll(left, right)
        else:
            return -1
    elif right.startswith('fdm'):
        return 1
    elif '@base' in left.lower():
        if '@base' in right.lower():
            return locale.strcoll(left, right)
        else:
            return -1
    elif '@base' in right.lower():
        return 1
    return locale.strcoll(left, right)


def read_config_matches(found_entries: Dict[str, Path],
                        expected_entry_type: str,
                        sub_path: Path,
                        matcher: Callable[[Path], bool] = lambda path: path.is_file() and path.suffix.lower() == '.json') -> List[Dict[str, str]]:
    pending_path_entries = {}
    new_items = []

    for entry in sub_path.iterdir():
        if VERBOSE: print(f'Checking {entry}')
        if matcher(entry):
            try:
                entry_data = json.loads(entry.read_text())
            except JSONDecodeError:
                print(f'Could not parse JSON in {entry}')
                continue
            entry_type = entry_data.get('type', None)
            entry_name = entry_data.get('name', None)
            if entry_type == expected_entry_type and entry_name and entry_name not in found_entries:
                if VERBOSE: print(f'{entry} matched')
                pending_path_entries[entry_name] = (entry, entry_data)
        else:
            if VERBOSE: print('... not a match')

    failed_dependency = False
    first_pass = len(found_entries) == 0

    if VERBOSE: print(f'Processing {len(pending_path_entries)} entries')

    while pending_path_entries and not failed_dependency:
        failed_dependency = True
        working_path_entries = dict(pending_path_entries)
        pending_path_entries = {}
        for entry_name in sorted(working_path_entries.keys(), key=cmp_to_key(anker_name_cmp)):
            entry, entry_data = working_path_entries[entry_name]
            if VERBOSE: print(f'Inspecting {entry}')
            if (first_pass and 'inherits' not in entry_data) or (not first_pass and entry_data['inherits'] in found_entries):
                failed_dependency = False
                found_entries[entry_name] = entry.relative_to(profile_path)
                new_items.append({'name': entry_name, 'sub_path': str(entry.relative_to(profile_path))})
            else:
                if VERBOSE: print(f'Saving {entry_name} for next pass')
                pending_path_entries[entry_name] = (entry, entry_data)
        
        first_pass = False

    if failed_dependency:
        global missing_deps
        missing_deps = failed_dependency
        print(f'Dependencies missing for the following:')
        for entry_name, entry_tuple in pending_path_entries.items():
            entry, entry_data = entry_tuple
            print(f' - [{entry_data["type"]}] {entry_name}: {str(entry.relative_to(profile_path))}')
    return new_items


def read_configs(expected_entry_type: str, sub_path: Path) -> List[Dict[str, str]]:
    found_entries: Dict[str, Path] = {}
    print(f'Loading {expected_entry_type} from {sub_path}...')
    if VERBOSE: print(f'...fdm')
    new_items = read_config_matches(
        found_entries,
        expected_entry_type,
        sub_path,
        lambda path: path.is_file() and path.suffix.lower() == '.json' and path.name.startswith('fdm'))
    
    if VERBOSE: print(f'...base')
    new_items.extend(
        read_config_matches(
            found_entries,
            expected_entry_type,
            sub_path,
            lambda path: path.is_file() and path.suffix.lower() == '.json' and '@base' in path.name.lower()))
    
    if VERBOSE: print(f'...defs')
    new_items.extend(read_config_matches(found_entries, expected_entry_type, sub_path))
    print(' - ' + '\n - '.join([str(item) for item in new_items]))
    
    return new_items
    

anker_data[machine_model_key] = read_configs('machine_model', machine_path)
anker_data[machine_key] = read_configs('machine', machine_path)
anker_data[process_key] = read_configs('process', process_path)
anker_data[filament_key] = read_configs('filament', filament_path)

if not missing_deps:
    input(f'Press Enter to write results to {json_path}')
    with open(json_path, 'w', encoding='utf-8') as f:
        json.dump(anker_data, f, ensure_ascii=False, indent=4)
else:
    print('Could not generate due to missing dependencies')

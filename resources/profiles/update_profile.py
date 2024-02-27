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
from typing import Callable, cast, Dict, List, Union

machine_model_key = 'machine_model_list'
machine_key = 'machine_list'
process_key = 'process_list'
filament_key = 'filament_list'

VERBOSE = False

script_path = Path(__file__).resolve().parent

def profile_name_cmp(left: str, right: str) -> int:
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


class VendorProfile:

    def __init__(self, vendor_name: str) -> None:
        self.json_path = script_path / f'{vendor_name}.json'
        self.manifest_data = cast(Dict[str, Union[str, List[Dict[str, str]]]], json.loads(self.json_path.read_text()))
        self.profile_path = script_path / vendor_name

        self.machine_path = self.profile_path / 'machine'
        self.process_path = self.profile_path / 'process'
        self.filament_path = self.profile_path / 'filament'

        self.found_machines = {}

        self.missing_deps: List[str, Path] = []

    def validate(self) -> bool:
        if not (self.json_path.exists() and self.json_path.is_file()):
            raise ValueError(f'Could not find vendor profile definition: {self.json_path}')
        if not (self.profile_path.exists() and self.profile_path.is_dir()):
            raise ValueError(f'Could not find vendor profile path: {self.profile_path}')

    def read_config_matches(self,
                            found_entries: Dict[str, Path],
                            expected_entry_type: str,
                            sub_path: Path,
                            matcher: Callable[[Path], bool] = lambda
                                    path: path.is_file() and path.suffix.lower() == '.json') -> List[Dict[str, str]]:
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

        # if we make it through the entire list of pending path entries and find
        # no entries that can be added that means all remaining entries have
        # unmet dependencies.
        failed_dependency = False
        first_pass = len(found_entries) == 0

        if VERBOSE: print(f'Processing {len(pending_path_entries)} entries')

        while pending_path_entries and not failed_dependency:
            failed_dependency = True
            working_path_entries = dict(pending_path_entries)
            pending_path_entries = {}
            for entry_name in sorted(working_path_entries.keys(), key=cmp_to_key(profile_name_cmp)):
                entry, entry_data = working_path_entries[entry_name]
                if VERBOSE: print(f'Inspecting {entry}')
                if (first_pass and 'inherits' not in entry_data) or (
                        not first_pass and entry_data['inherits'] in found_entries):
                    failed_dependency = False
                    found_entries[entry_name] = entry.relative_to(self.profile_path)
                    new_items.append({'name': entry_name, 'sub_path': str(entry.relative_to(self.profile_path))})
                else:
                    if VERBOSE: print(f'Saving {entry_name} for next pass')
                    pending_path_entries[entry_name] = (entry, entry_data)

            first_pass = False

        if failed_dependency:
            self.missing_deps.extend(list(pending_path_entries.items()))
        return new_items

    def read_configs(self,
                     expected_entry_type: str,
                     sub_path: Path) -> List[Dict[str, str]]:
        found_entries: Dict[str, Path] = {}
        print(f'Loading {expected_entry_type} from {sub_path}...')
        if VERBOSE: print(f'...fdm')
        new_items = self.read_config_matches(
            found_entries,
            expected_entry_type,
            sub_path,
            lambda path: path.is_file() and path.suffix.lower() == '.json' and path.name.startswith('fdm'))

        if VERBOSE: print(f'...base')
        new_items.extend(
            self.read_config_matches(
                found_entries,
                expected_entry_type,
                sub_path,
                lambda path: path.is_file() and path.suffix.lower() == '.json' and '@base' in path.name.lower()))

        if VERBOSE: print(f'...defs')
        new_items.extend(self.read_config_matches(found_entries, expected_entry_type, sub_path))
        print(' - ' + '\n - '.join([str(item) for item in new_items]))

        return new_items

    def generate_manifest(self) -> Dict[str, Union[str, List[Dict[str, str]]]]:
        manifest_data = dict(self.manifest_data)
        manifest_data[machine_model_key] = self.read_configs('machine_model', self.machine_path)
        manifest_data[machine_key] = self.read_configs('machine', self.machine_path)
        manifest_data[process_key] = self.read_configs('process', self.process_path)
        manifest_data[filament_key] = self.read_configs('filament', self.filament_path)
        return manifest_data

    def write(self, manifest_data: Dict[str, Union[str, List[Dict[str, str]]]]) -> None:
        with open(self.json_path, 'w', encoding='utf-8') as f:
            json.dump(manifest_data, f, ensure_ascii=False, indent=4)

def main():
    global VERBOSE

    cli_args = ArgumentParser()
    cli_args.add_argument('vendor', help='Vendor name. (Anker, BBL, etc)')
    cli_args.add_argument('--verbose', '-v', action="store_true", default=False)

    args = cli_args.parse_args()

    VERBOSE = args.verbose

    vendor_profile = VendorProfile(args.vendor)
    try:
        vendor_profile.validate()
    except ValueError as ex:
        print(ex)
        return

    manifest_data = vendor_profile.generate_manifest()

    if vendor_profile.missing_deps:
        print(f'The following profile definitions had missing dependencies:')
        for entry_name, entry_tuple in vendor_profile.missing_deps:
            entry, entry_data = entry_tuple
            print(f'{entry.relative_to(vendor_profile.profile_path)} [{entry_data["type"]}] {entry_name}: {str(entry.relative_to(profile_path))}')
    else:
        input(f'Press Enter to write results to {vendor_profile.json_path}')
        vendor_profile.write(manifest_data)


if __name__ == '__main__':
    main()

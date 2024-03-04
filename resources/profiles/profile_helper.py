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
from typing import Callable, cast, Dict, List, Optional, Tuple, Union

machine_model_key = 'machine_model_list'
machine_key = 'machine_list'
process_key = 'process_list'
filament_key = 'filament_list'

VERBOSE = 0

VERBOSE_SILENT = 0
VERBOSE_ERR = 1
VERBOSE_WARN = 2
VERBOSE_INFO = 3
VERBOSE_DEBUG = 4

script_path = Path(__file__).resolve().parent


# descriptive data types
NamePathMap = Dict[str, str]
ManifestSection = List[NamePathMap]
ManifestData = Dict[str, Union[str, ManifestSection]]
ProfileData = Dict[str, Union[str, List[str]]]
ProfileNameFileEntryMap = Dict[str, Tuple[Path, ManifestData]]
ProfileNameFileEntry = Tuple[str, Tuple[Path, ManifestData]]


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


def pout(level: int, msg: str) -> None:
    if VERBOSE >= level:
        print(msg)


class VendorProfile:

    def __init__(self, vendor_name: str) -> None:
        self.json_path = script_path / f'{vendor_name}.json'
        self.profile_path = script_path / vendor_name

        self.manifest_data: Optional[ManifestData] = None

        self.machine_path = self.profile_path / 'machine'
        self.process_path = self.profile_path / 'process'
        self.filament_path = self.profile_path / 'filament'

    def validate(self) -> bool:
        if not (self.json_path.exists() and self.json_path.is_file()):
            raise ValueError(f'Could not find vendor profile definition: {self.json_path}')
        if not (self.profile_path.exists() and self.profile_path.is_dir()):
            raise ValueError(f'Could not find vendor profile path: {self.profile_path}')
        return True

    def read_config_matches(self,
                            found_entries: Dict[str, Path],
                            expected_entry_type: str,
                            sub_path: Path,
                            matcher: Callable[[Path], bool] = lambda
                                    path: path.is_file() and path.suffix.lower() == '.json',
                            ) -> Tuple[ManifestSection, List[ProfileNameFileEntry]]:
        pending_path_entries: ProfileNameFileEntryMap = {}
        new_items: ManifestSection = []

        pout(VERBOSE_INFO, f'Searching for profiles in {sub_path.resolve()} ')
        for entry in sub_path.iterdir():
            pout(VERBOSE_DEBUG, f'Checking {entry}')
            if matcher(entry):
                try:
                    entry_data = cast(ProfileData, json.loads(entry.read_text()))
                except JSONDecodeError:
                    if VERBOSE >= VERBOSE_ERR: print(f'Could not parse JSON in {entry}')
                    continue
                entry_type = cast(str, entry_data.get('type', None))
                entry_name = cast(str, entry_data.get('name', None))
                if entry_type == expected_entry_type and entry_name and entry_name not in found_entries:
                    pout(VERBOSE_DEBUG, f'{entry} matched')
                    pending_path_entries[entry_name] = (entry, entry_data)
            else:
                pout(VERBOSE_DEBUG, '... not a match')

        # if we make it through the entire list of pending path entries and find
        # no entries that can be added that means all remaining entries have
        # unmet dependencies.
        failed_dependency = False
        first_pass = len(found_entries) == 0

        pout(VERBOSE_INFO, f'Processing {len(pending_path_entries)} profiles')

        while pending_path_entries and not failed_dependency:
            failed_dependency = True
            working_path_entries = dict(pending_path_entries)
            pending_path_entries = {}
            for entry_name in sorted(working_path_entries.keys(), key=cmp_to_key(profile_name_cmp)):
                entry, entry_data = working_path_entries[entry_name]
                pout(VERBOSE_DEBUG, f'Inspecting {entry}')
                if (first_pass and 'inherits' not in entry_data) or (
                        not first_pass and entry_data['inherits'] in found_entries):
                    failed_dependency = False
                    found_entries[entry_name] = entry.relative_to(self.profile_path)
                    new_items.append({'name': entry_name, 'sub_path': str(entry.relative_to(self.profile_path))})
                else:
                    pout(VERBOSE_DEBUG, f'Saving {entry_name} for next pass')
                    pending_path_entries[entry_name] = (entry, entry_data)

            first_pass = False

        pout(VERBOSE_INFO, f'Loadable profiles: {len(new_items)}\n'
                           f'Profiles with unmet dependencies: {len(pending_path_entries)}')

        return new_items, list(pending_path_entries.items())

    def read_configs(self,
                     expected_entry_type: str,
                     sub_path: Path) -> Tuple[List[NamePathMap], List[ProfileNameFileEntry]]:
        found_entries: Dict[str, Path] = {}
        pout(VERBOSE_INFO, f'Loading {expected_entry_type} from {sub_path}...')
        pout(VERBOSE_DEBUG, f'...fdm')
        new_items, missing_deps = self.read_config_matches(
            found_entries,
            expected_entry_type,
            sub_path,
            lambda path: path.is_file() and path.suffix.lower() == '.json' and path.name.startswith('fdm'))

        pout(VERBOSE_DEBUG, f'...base')
        base_items, base_missing = self.read_config_matches(
                found_entries,
                expected_entry_type,
                sub_path,
                lambda path: path.is_file() and path.suffix.lower() == '.json' and '@base' in path.name.lower())
        new_items.extend(base_items)
        missing_deps.extend(base_missing)

        pout(VERBOSE_DEBUG, f'...defs')
        def_items, def_missing = self.read_config_matches(found_entries, expected_entry_type, sub_path)
        new_items.extend(def_items)
        missing_deps.extend(def_missing)

        pout(VERBOSE_DEBUG, 'Found:\n - ' + '\n - '.join([str(item) for item in new_items]))

        return new_items, missing_deps

    def read_profiles(self) -> Tuple[ManifestData, List[ProfileNameFileEntry]]:
        self.manifest_data = cast(ManifestData, json.loads(self.json_path.read_text()))
        manifest_data = dict(self.manifest_data)
        unmet_deps: List[ProfileNameFileEntry] = []
        for key, entry_type, sub_path in [(machine_model_key, 'machine_model', self.machine_path),
                                          (machine_key, 'machine', self.machine_path),
                                          (process_key, 'process', self.process_path),
                                          (filament_key, 'filament', self.filament_path)]:
            matched, missing = self.read_configs(entry_type, sub_path)
            manifest_data[key] = matched
            unmet_deps.extend(missing)
        return manifest_data, unmet_deps

    def write(self, manifest_data: Dict[str, Union[str, List[Dict[str, str]]]]) -> None:
        print(f'Updating {self.json_path}')
        with open(self.json_path, 'w', encoding='utf-8') as f:
            json.dump(manifest_data, f, ensure_ascii=False, indent=4)


def main() -> int:
    global VERBOSE

    cli_args = ArgumentParser()
    cli_args.add_argument('vendor', help='Vendor name. (Anker, BBL, etc)')
    cli_args.add_argument('--verbose', '-v', action="count", default=0)
    cli_args.add_argument('--update', '-u', action='store_true', default=False)

    args = cli_args.parse_args()

    VERBOSE = args.verbose + 1

    vendor_profile = VendorProfile(args.vendor)
    try:
        vendor_profile.validate()
    except ValueError as ex:
        print(ex)
        return -1

    manifest_data, missing_deps = vendor_profile.read_profiles()

    sections = (machine_model_key, machine_key, filament_key, process_key)
    # First check that a matching file exists for every manifest entry
    manifest_missing: Dict[str, ManifestSection] = {}
    pout(VERBOSE_ERR, f'Validating {vendor_profile.json_path}...')
    mismatch = False
    for section in sections:
        section_entries = dict((entry['name'], entry['sub_path']) for entry in vendor_profile.manifest_data[section])
        found_entries = dict((entry['name'], entry['sub_path']) for entry in manifest_data[section])
        missing_entries: Dict[str, str] = {}
        for entry_name, path in section_entries.items():
            if entry_name not in found_entries or not (vendor_profile.profile_path / path).exists():
                missing_entries[entry_name] = path
        if missing_entries:
            mismatch = True
            print(f'[{section}] Manifest entries with missing files:')
            print(' - ' + '\n - '.join([f'{name}: {path}' for name, path in missing_entries.items()]))
        unlisted_entries: Dict[str, str] = {}
        for entry_name, path in found_entries.items():
            if entry_name not in section_entries or section_entries[entry_name] != path:
                unlisted_entries[entry_name] = path
        if unlisted_entries:
            mismatch = True
            print(f'[{section}] Profiles found but missing in manifest:')
            print(' - ' + '\n - '.join([f'{name}: {path}' for name, path in unlisted_entries.items()]))

    if mismatch:
        if missing_deps:
            print(f'The following profile definitions had missing dependencies:')
            for entry_name, entry_tuple in missing_deps:
                entry, entry_data = entry_tuple
                print(f'{entry.relative_to(vendor_profile.profile_path)} [{entry_data["type"]}] {entry_name}: {str(entry.relative_to(profile_path))}', file=sys.stderr)
                return -1
        else:
            sync = input(f'Type SYNC to synchronize manifest to match profiles {vendor_profile.json_path}') if not args.update else 'SYNC'
            if sync == 'SYNC':
                vendor_profile.write(manifest_data)
                return 0
            else:
                return 1
    else:
        return 0


if __name__ == '__main__':
    exit(main())

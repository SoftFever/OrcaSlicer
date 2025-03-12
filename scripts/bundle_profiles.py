#!/usr/bin/env python3

import json
import argparse
import os
import shutil

def bundle_profile(base_dir, item):
    # Read the file content specified by 'sub_path' then store them into 'content' field
    sub_profile_path = os.path.join(base_dir, item['sub_path'])
    with open(sub_profile_path, 'r', encoding='utf-8') as f:
        item['content'] = json.load(f)


def bundle_category(base_dir, vendor_profile, category):
    if category not in vendor_profile:
        return
    
    cat = vendor_profile[category]
    for item in cat:
        bundle_profile(base_dir, item)


def process_vendor(path, dest):
    print('Bundling', path)
    with open(path, 'r', encoding='utf-8') as f:
        vendor_profile = json.load(f)

    # Make sure it's vendor profile
    if 'name' not in vendor_profile or 'version' not in vendor_profile:
        return
    
    vendor_name = os.path.splitext(os.path.basename(path))[0]
    base_dir = os.path.join(os.path.dirname(path), vendor_name)
    
    for category in ['machine_model_list', 'process_list', 'filament_list', 'machine_list']:
        bundle_category(base_dir, vendor_profile, category)

    # Save bundle file
    vendor_profile['bundle'] = True
    dest_path = os.path.join(dest, f'{vendor_name}.bundle.json')
    with open(dest_path, 'w', encoding='utf-8') as f:
        json.dump(vendor_profile, f, indent=4, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description='Bundle system profile json files into single bundle file')
    parser.add_argument('-s', '--src', required=True, type=str, help='path to the folder contains all system profiles, usually the "resources\profiles" folder')
    parser.add_argument('-d', '--dest', required=True, type=str, help='path to the folder where bundled profiles are stored')

    args = parser.parse_args()

    os.makedirs(args.dest, exist_ok=True)

    for root, dirs, files in os.walk(args.src):
        for file in files:
            if file.lower().endswith('.json') and not file.lower().endswith('.bundle.json'):
                full_path = os.path.join(root, file)
                try:
                    process_vendor(full_path, args.dest)
                except Exception as e:
                    print(f"Error processing {full_path}")
                    raise
                
        break

if __name__ == '__main__':
    main()

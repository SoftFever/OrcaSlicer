#!/usr/bin/env python3
"""Simulate the mobile app's scoped-vendor install for every printer model in
every vendor bundle, and JSON-parse every file that would be downloaded. Fails
loudly when a file parses badly, is missing, or a scope resolves empty.

Mobile installs are scoped: picking one printer only pulls that model's machine
variants + any filament / process file NOT tagged for a different model + the
STL/SVG assets the model's machine_model JSON references. If any of those files
are broken upstream, the mobile app boots with a "Profile load failed" banner
(see the Qidi Q1 Pro.json JSON parse error the user hit on device). This script
runs the same filter against the resources/profiles tree, exits non-zero when
anything is off, and prints a summary that CI turns into a GitHub check.

Run locally: `python3 scripts/mobile_profile_smoke.py`.
Run in CI:   `.github/workflows/mobile_profile_smoke.yml` (nightly + on push).
"""

import json
import re
import sys
from pathlib import Path

REPO_ROOT     = Path(__file__).resolve().parent.parent
PROFILES_DIR  = REPO_ROOT / 'resources' / 'profiles'
# Folders inside a vendor tree that the mobile install treats as "model-scoped"
# (files ending in .json get filtered by @<other-model> markers).
SCOPED_FOLDERS = {'machine', 'filament', 'process'}


def _re_escape(s: str) -> str:
    return re.escape(s)


def excludes_other_models_regex(selected_model: str, all_models: list[str]) -> re.Pattern[str] | None:
    """Mirror of makeOtherModelFilter in mobile/apps/orca/lib/profiles.ts. A
    filename belongs to another model when it starts with "<M> " (machine
    variants like `Qidi X-Max 4 0.4 nozzle.json`) or contains "@<M>" (process /
    filament like `0.16mm Optimal @X-Max 4 0.4 nozzle.json`). The trailing token
    (last space-separated word onward) also counts so both `Qidi X-Max 4` and
    `X-Max 4` are recognized. Tokens that are proper prefixes of the selected
    model are dropped so we do not shadow the model we are keeping."""
    others = [m for m in all_models if m != selected_model]
    if not others:
        return None
    tokens: set[str] = set()
    for m in others:
        tokens.add(m)
        trailing = re.sub(r'^\S+\s+', '', m)
        if trailing != m and trailing:
            tokens.add(trailing)
    selected_trailing = re.sub(r'^\S+\s+', '', selected_model)
    for t in list(tokens):
        if selected_model.startswith(t + ' ') or selected_trailing.startswith(t + ' '):
            tokens.discard(t)
    if not tokens:
        return None
    alternation = '|'.join(_re_escape(t) for t in tokens)
    return re.compile(rf'(?:@\s*|^)(?:{alternation})(?=\s|\.|$)', re.IGNORECASE)


def load_json(path: Path) -> tuple[dict | None, str | None]:
    try:
        return json.loads(path.read_text(encoding='utf-8')), None
    except json.JSONDecodeError as e:
        return None, f'{path.relative_to(REPO_ROOT)}: {e}'
    except OSError as e:
        return None, f'{path.relative_to(REPO_ROOT)}: {e}'


def files_for_model(vendor_dir: Path, manifest: dict, model_name: str) -> tuple[list[Path], list[str]]:
    """Return every path the mobile install would download for `model_name`,
    plus the STL/SVG asset filenames the model's machine_model JSON references
    (so we can check they exist without walking every binary in vendor_dir)."""
    all_models = [
        m.get('name', '') for m in manifest.get('machine_model_list', []) if m.get('name')
    ]
    excludes = excludes_other_models_regex(model_name, all_models)
    selected_entry = next(
        (m for m in manifest.get('machine_model_list', []) if m.get('name') == model_name),
        None,
    )
    model_assets: set[str] = set()
    if selected_entry and (sub_path := selected_entry.get('sub_path')):
        model_json_path = vendor_dir / sub_path
        if model_json_path.is_file():
            model_json, _ = load_json(model_json_path)
            if isinstance(model_json, dict):
                for key in ('bed_model', 'bed_texture', 'hotend_model'):
                    v = model_json.get(key)
                    if isinstance(v, str) and v:
                        model_assets.add(v)
    kept: list[Path] = [vendor_dir.with_suffix('.json')]  # vendor index file
    for path in sorted(vendor_dir.rglob('*')):
        if not path.is_file():
            continue
        rel  = path.relative_to(vendor_dir)
        top  = rel.parts[0] if rel.parts else ''
        name = path.name
        is_root_binary = len(rel.parts) == 1 and path.suffix.lower() in {
            '.stl', '.svg', '.png', '.jpg', '.jpeg', '.glb', '.obj', '.dxf',
        }
        if is_root_binary:
            if name not in model_assets:
                continue
            kept.append(path)
            continue
        if top in SCOPED_FOLDERS and excludes is not None and excludes.search(name):
            continue
        kept.append(path)
    return kept, sorted(model_assets)


def main() -> int:
    if not PROFILES_DIR.is_dir():
        print(f'error: profiles dir not found at {PROFILES_DIR}', file=sys.stderr)
        return 2

    vendor_indices = sorted(PROFILES_DIR.glob('*.json'))
    vendors = [(p.stem, p) for p in vendor_indices]

    total_models = 0
    total_files  = 0
    errors: list[str] = []

    for vendor_name, index_path in vendors:
        vendor_dir = PROFILES_DIR / vendor_name
        if not vendor_dir.is_dir():
            continue
        manifest, err = load_json(index_path)
        if err is not None:
            errors.append(f'[vendor index] {err}')
            continue
        assert manifest is not None
        models = [m.get('name') for m in manifest.get('machine_model_list', []) if m.get('name')]
        if not models:
            continue
        for model in models:
            total_models += 1
            kept, assets = files_for_model(vendor_dir, manifest, model)
            if len(kept) < 2:  # index + at least one model file
                errors.append(f'[{vendor_name} / {model}] scoped install would pull no files')
                continue
            for path in kept:
                total_files += 1
                if path.suffix.lower() != '.json':
                    if not path.is_file():
                        errors.append(f'[{vendor_name} / {model}] missing asset: {path.name}')
                    continue
                _, perr = load_json(path)
                if perr is not None:
                    errors.append(f'[{vendor_name} / {model}] {perr}')
            for asset in assets:
                if not (vendor_dir / asset).is_file():
                    errors.append(f'[{vendor_name} / {model}] machine_model references missing asset: {asset}')

    print(f'Checked {len(vendors)} vendors, {total_models} models, {total_files} file resolutions.')
    if errors:
        print(f'FAILED — {len(errors)} error(s):')
        for e in errors:
            print(f'  - {e}')
        return 1
    print('All scoped installs resolve cleanly.')
    return 0


if __name__ == '__main__':
    sys.exit(main())

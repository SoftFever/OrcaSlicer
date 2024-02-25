import json
from pathlib import Path
from typing import Dict, List


scripts_dir = Path(__file__).resolve().parent
print(f'Scripts dir: {scripts_dir}')
root_dir = scripts_dir.parent
profiles_dir = root_dir / 'resources' / 'profiles'

printers: Dict[str, List[str]] = {}

print(f'Looking in {profiles_dir.resolve()}')
for entry in profiles_dir.glob('*.json'):
    if entry.is_file():
        entry_info = json.loads(entry.read_text())
        vendor_name = entry_info.get('name', None)
        if vendor_name:
            models = [machine.get('name', None) for machine in entry_info.get('machine_model_list', []) if machine.get('name', None)]
            printers[vendor_name] = models

vendor_names = [f'"{vendor_name}",' for vendor_name in sorted(printers.keys(), key=str.casefold)]
vend_col_width = len(max(vendor_names, key=len))
vendors_formatted = '    {' + '\n     '.join(' '.join(f"{vendor_name:{vend_col_width}}" for vendor_name in vendor_names[i:i+5]) for i in range(0, len(vendor_names), 5)).rstrip()[:-1] + '}'
print(vendors_formatted)


models_formatted = '    {'
models_indent = len(models_formatted) + vend_col_width + 2
for vendor_name in sorted(printers.keys(), key=str.casefold):
    vendor_formatted = f'"{vendor_name}",'
    models_formatted += f'{{{vendor_formatted:{vend_col_width}}{{'

    model_names = printers[vendor_name]
    model_names_formatted = [f'"{model_name}",' for model_name in model_names]
    model_col_width = len(max(model_names_formatted, key=len))
    model_names_str = ('\n' + ' ' * models_indent).join(' '.join(f"{model_name:{model_col_width}}" for model_name in model_names_formatted[i:i+5]) for i in range(0, len(model_names), 5)).rstrip()[:-1] + '}'

    models_formatted += model_names_str
    
    models_formatted += '},\n     '

models_formatted = models_formatted.rstrip()[:-1]
print(models_formatted)

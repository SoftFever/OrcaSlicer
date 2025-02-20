#!/usr/bin/env python3

import argparse
import pymeshlab
from os import path
import sys

parser = argparse.ArgumentParser(description='Model fix script for OrcaSlicer')
parser.add_argument('-i', dest='input', help='Input mesh')
parser.add_argument('-o', dest='output', help='Output mesh')
parser.add_argument('-s', dest='script', help='Meshlab script');
parser.add_argument('--version', action='store_const', const=True, default=False, help='Print PyMeshLab version');
args = parser.parse_args()
#print(args)

if (args.version):
    pymeshlab.print_pymeshlab_version()
    exit(0)

scriptname = path.join(path.dirname(path.abspath(sys.argv[0])), 'meshlab-model-fix.mlx') if args.script is None else args.script

print("Meshlab script: {}".format(scriptname))
print("Input mesh: {}".format(args.input))
print("Output mesh: {}".format(args.output))

ms = pymeshlab.MeshSet()
ms.set_verbosity(True)

ms.load_new_mesh(args.input)
ms.load_filter_script(scriptname)
ms.apply_filter_script()
ms.save_current_mesh(args.output)

exit(0)


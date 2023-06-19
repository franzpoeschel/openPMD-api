#!/usr/bin/env python3

from __future__ import print_function

import argparse
import hpccm
from hpccm.primitives import copy, baseimage, shell
import multiprocessing

parser = argparse.ArgumentParser(description='HPCCM Tutorial')
parser.add_argument(
    '--format',
    type=str,
    default='singularity',
    choices=['docker', 'singularity'],
    help='Container specification format (default: singularity)')
args = parser.parse_args()

library_image = 'quay.io/pypa/manylinux2014_x86_64'

### Start "Recipe"
Stage0 = hpccm.Stage()
Stage0 += baseimage(image=library_image)

Stage0 += copy(src='../library_builders.sh', dest='/opt')

Stage0 += shell(commands=[
    'yum -y install python3',
    'cd /opt',
    'export CPU_COUNT={}'.format(multiprocessing.cpu_count()),
    'bash library_builders.sh'
])

hpccm.config.set_container_format(args.format)

print(Stage0)
print("\n")

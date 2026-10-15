#!/usr/bin/env python3
"""Validate the current C ABI and its intentional package compatibility floor.

Frozen clients remain archived. They are not loaded against ABI 0.5: their
public field offsets belong to an incompatible library identity.
"""
import argparse
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build'))
    args = parser.parse_args()
    subprocess.run(['ctest', '--test-dir', str(args.build), '--output-on-failure',
                    '-R', '^capi_(library_(shared|static)|descriptors|package_compatibility)$'], check=True)


if __name__ == '__main__':
    main()

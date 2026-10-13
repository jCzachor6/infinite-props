#!/usr/bin/env python3
"""Verify RGBA8 pins using exact rational Porter-Duff probabilities.

This oracle does not call the runtime or use its integer implementation.
The grid covers every source/destination alpha pair with unequal RGB channels.
"""
from fractions import Fraction
import hashlib
from pathlib import Path
import sys


def nearest(value):
    return (value + Fraction(1, 2)).numerator // (value + Fraction(1, 2)).denominator


def source_over(dst, src):
    source = Fraction(src[3], 255)
    destination = Fraction(dst[3], 255) * (1 - source)
    alpha = source + destination
    if not alpha:
        return (0, 0, 0, 0)
    return tuple(nearest((s * source + d * destination) / alpha)
                 for d, s in zip(dst[:3], src[:3])) + (nearest(alpha * 255),)


def reference_grid():
    return bytes(channel for ad in range(256) for a in range(256)
                 for channel in source_over((201, 4, 172, ad), (17, 213, 88, a)))


def main():
    target = Path(__file__).resolve().parent.parent / 'tests/fixtures/source-over-v0.6.rgba'
    expected = reference_grid()
    if sys.argv[1:] == ['--write']:
        target.write_bytes(expected)
    else:
        assert not sys.argv[1:], 'usage: check_source_over_reference.py [--write]'
        assert target.read_bytes() == expected, 'RGBA8 reference differs from exact rational oracle'
    print('Source-over reference: 65,536 alpha pairs, SHA256 ' + hashlib.sha256(expected).hexdigest())


if __name__ == '__main__':
    main()

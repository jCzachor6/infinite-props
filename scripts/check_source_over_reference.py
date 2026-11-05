#!/usr/bin/env python3
"""Verify RGBA8 pins using exact rational Porter-Duff probabilities.

This oracle does not call the runtime or use its integer implementation.
The grid covers every source/destination alpha pair with unequal RGB channels.
"""
import base64
from fractions import Fraction
import hashlib
import json
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


# Pins the oracle itself: a change here must be reviewed, never regenerated blindly.
GRID_SHA256 = 'a011f9f4092fd0ef15253e30cd44a925cde66602b760364bcba1a8dd1450036a'


def main():
    args = sys.argv[1:]
    assert args in ([], ['--print']), 'usage: check_source_over_reference.py [--print]'
    expected = reference_grid()
    if args:
        json.dump({'rgba_base64': base64.b64encode(expected).decode()}, sys.stdout)
        return
    assert hashlib.sha256(expected).hexdigest() == GRID_SHA256, 'rational source-over oracle changed'
    print('Source-over reference: 65,536 alpha pairs, SHA256 ' + GRID_SHA256)


if __name__ == '__main__':
    main()

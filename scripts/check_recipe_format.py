#!/usr/bin/env python3
"""Check the stored gallery through the production formatter and compiler."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('compiler', type=Path)
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    gallery = Path(__file__).resolve().parents[1] / 'examples/recipes'
    sources = sorted(gallery.glob('*.ips'))
    if len(sources) != 24:
        raise AssertionError('the recipe gallery must contain 24 sources')

    def run(*arguments):
        result = subprocess.run([str(compiler), *map(str, arguments)], capture_output=True,
                                timeout=15, check=False)
        if result.returncode:
            raise AssertionError(result.stderr.decode('utf-8', 'replace'))

    with tempfile.TemporaryDirectory(prefix='iprop-recipes-') as directory:
        output = Path(directory)
        for source in sources:
            canonical = output / source.name
            again = output / (source.stem + '.again.ips')
            before = output / (source.stem + '.before.iprop')
            after = output / (source.stem + '.after.iprop')
            run(source, '--format-only', '--format', canonical)
            if canonical.read_bytes() != source.read_bytes():
                raise AssertionError(source.name + ': stored source is not canonical')
            run(canonical, '--format-only', '--format', again)
            if again.read_bytes() != canonical.read_bytes():
                raise AssertionError(source.name + ': formatting is not idempotent')
            run(source, '--strip', '--no-compress', '-o', before)
            run(canonical, '--strip', '--no-compress', '-o', after)
            if before.read_bytes() != after.read_bytes():
                raise AssertionError(source.name + ': formatting changed stripped bytes')
    print('24 recipes: canonical, idempotent, identical stripped bytes')


if __name__ == '__main__':
    main()

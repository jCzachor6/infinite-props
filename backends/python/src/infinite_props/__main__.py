import argparse
from dataclasses import asdict
import json

from . import Program, Color, F32, U32, I32


def main():
    parser = argparse.ArgumentParser(description="Render a compiled .iprop with the native runtime and Pillow")
    parser.add_argument("program")
    parser.add_argument("output")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--library")
    parser.add_argument("--input", action="append", default=[], metavar="NAME=VALUE")
    args = parser.parse_args()
    with Program(args.program, library=args.library) as program:
        schema = {item.name: item.default_value for item in program.inputs}
        overrides = {}
        for entry in args.input:
            name, separator, text = entry.partition("=")
            if not separator or name not in schema or name in overrides:
                parser.error("Each --input must name a unique declared input: " + entry)
            try:
                if isinstance(schema[name], Color):
                    if not text.startswith("#") or len(text) not in (7, 9):
                        raise ValueError("Use #RRGGBB or #RRGGBBAA")
                    channels = bytes.fromhex(text[1:])
                    overrides[name] = Color(*channels)
                elif isinstance(schema[name], F32):
                    overrides[name] = F32(float(text))
                else:
                    number = int(text, 0)
                    overrides[name] = I32(number) if number < 0 else U32(number)
            except (ValueError, OverflowError) as error:
                parser.error(str(error))
        image = program.generate(args.seed, inputs=overrides)
    image.to_pillow().save(args.output)
    print(json.dumps({"width": image.width, "height": image.height, "alpha_mode": image.alpha_mode,
        "outputs": [asdict(slot) for slot in image.outputs], "statistics": asdict(image.statistics)}, ensure_ascii=False))


if __name__ == "__main__":
    main()

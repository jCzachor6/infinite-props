import argparse
from dataclasses import asdict
import json

from . import Program, Color, F32, U32, I32, ImageKind, OutputKind

MAPS = (ImageKind.ROUGHNESS, ImageKind.METALLIC, ImageKind.AO, ImageKind.EMISSION)


def main():
    parser = argparse.ArgumentParser(description="Render a compiled .iprop with the native runtime and Pillow")
    parser.add_argument("program")
    parser.add_argument("output")
    parser.add_argument("--seed", type=int, default=0)
    parser.add_argument("--library")
    parser.add_argument("--input", action="append", default=[], metavar="NAME=VALUE")
    parser.add_argument("--normal-output", metavar="PATH", help="also save the normal map")
    for kind in MAPS:
        name = kind.name.lower()
        parser.add_argument("--" + name + "-output", metavar="PATH", help="also save the " + name + " map")
    parser.add_argument("--derive-normal", action="store_true",
                        help="derive a normal map when the program publishes none")
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
        image = program.generate(args.seed, inputs=overrides, derive_normal=args.derive_normal)
    if any(slot.kind == OutputKind.PART for slot in image.outputs):
        parser.error("part outputs are unsupported by the CLI JSON format; use Program.generate() for typed resources")
    if args.normal_output and image.normal is None:
        parser.error("the program publishes no normal map; --derive-normal derives one")
    maps = {kind: getattr(args, kind.name.lower() + "_output") for kind in MAPS}
    for kind, path in maps.items():
        if path and image.image_of(kind) is None:
            parser.error("the program publishes no " + kind.name.lower() + " map")
    image.to_pillow().save(args.output)
    if args.normal_output:
        image.normal.to_pillow().save(args.normal_output)
    for kind, path in maps.items():
        if path:
            image.image_of(kind).to_pillow().save(path)
    report = {"width": image.width, "height": image.height, "alpha_mode": image.alpha_mode,
        "outputs": [{key: value for key, value in asdict(slot).items() if key != "parts"} for slot in image.outputs],
        "statistics": asdict(image.statistics)}
    if image.images:  # absent for a program that publishes none, as before
        report["images"] = [{"kind": item.kind.name.lower(), "source": item.source.name.lower(), "name": item.name}
                            for item in image.images]
    print(json.dumps(report, ensure_ascii=False))


if __name__ == "__main__":
    main()

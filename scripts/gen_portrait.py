#!/usr/bin/env python3
"""Generates examples/portrait.ipa — a 1:1 port of QuantSpace's character
portrait generation (PaCharacter::generate + PaPortrait::generate).

Every constant, coordinate, RNG draw order and paint order below is copied
verbatim from base-code/QuantSpace/src/procedural/characters/. The script
emits .ipa assembly text; run tools/ipa to assemble it.

Usage: python3 scripts/gen_portrait.py > examples/portrait.ipa
"""

import math
import sys

out = []


def emit(line=""):
    out.append(line)


def color(hex_s):
    emit(f"  const.color #{hex_s}ff")


def f32(v):
    # v may already be a repr'd string; emit the bare numeric literal.
    text = v.strip("'") if isinstance(v, str) else repr(float(v))
    emit(f"  const.f32 {text}")


def i32(v):
    emit(f"  const.i32 {v}")


def op(name, operand=None):
    emit(f"  {name}{' ' + str(operand) if operand is not None else ''}")


# ---------------------------------------------------------------------------
# QuantSpace palettes (PaCharacter.cpp), verbatim.
# ---------------------------------------------------------------------------
SKIN = [(245, 205, 170), (228, 177, 139), (209, 151, 112), (187, 127, 89),
        (158, 101, 71), (126, 79, 59), (101, 64, 49), (232, 190, 166)]
HAIR = [(34, 30, 34), (69, 44, 33), (105, 67, 40), (166, 87, 43),
        (205, 164, 91), (226, 204, 155), (119, 116, 121), (208, 209, 199)]
EYES = [(75, 111, 125), (75, 105, 72), (105, 72, 43), (52, 46, 39), (131, 140, 143)]
CLOTHING = [(55, 87, 109), (68, 109, 101), (125, 79, 64), (109, 99, 132),
            (123, 131, 118), (67, 78, 92)]
ACCENT = [(144, 197, 185), (224, 161, 74)]

OUTLINE = "1b1e25"          # {27,30,37}
EYEWHITE = "dcd8c7"          # {220,216,199}
GLINT = "f5eed9"             # {245,238,217}
MOUTHMIX = "723434"          # {114,52,52}
BADGELINE = "b8c6c1"         # {184,198,193}
CORNER = "576f77"            # {87,111,119}
BG_LOW = "141d27"            # {20,29,39}
BG_HIGH = "314952"           # {49,73,82}


def hexc(c):
    return "%02x%02x%02x" % c


# ---------------------------------------------------------------------------
# Palette pickers: nested if/else chains over the index.
# ---------------------------------------------------------------------------
def emit_picker(name, palette):
    emit(f"fn {name} ( i32 ) -> ( color ):")
    emit("  locals: color")
    # Default: the last colour; each matching index overwrites it.
    color(hexc(palette[-1]))
    op("local.set", 1)
    for index in range(len(palette) - 1):
        op("local.get", 0)
        i32(index)
        op("i32.eq")
        op("if")
        color(hexc(palette[index]))
        op("local.set", 1)
        op("end")
    op("local.get", 1)
    emit("  return")
    emit("endfn")
    emit("")


# ---------------------------------------------------------------------------
# Age sampling (PaCharacterAge.cpp): discrete Gaussian CDF over 18..40.
# ---------------------------------------------------------------------------
AGES = list(range(18, 41))
vals = [math.exp(-0.5 * ((a - 29.0) / 8.0) ** 2) for a in AGES]
total = sum(vals)
cumulative = []
acc = 0.0
for v in vals:
    acc += v
    cumulative.append(acc / total)


def emit_age():
    emit("fn ageFromUnit ( f32 ) -> ( i32 ):")
    emit("  locals: i32")
    i32(AGES[-1])
    op("local.set", 1)
    # Later assignments win, so visit broad thresholds before narrow ones.
    for index in reversed(range(len(AGES) - 1)):
        op("local.get", 0)
        f32(repr(cumulative[index]))
        op("f32.lt")
        op("if")
        i32(AGES[index])
        op("local.set", 1)
        op("end")
    op("local.get", 1)
    emit("  return")
    emit("endfn")
    emit("")


# ---------------------------------------------------------------------------
# The hairstyle switch (PaPortrait.cpp).
# drawHair: local 0 = style, local 1 = hair colour, caller local 8 = accent.
# ---------------------------------------------------------------------------
def emit_hairstyles():
    emit("fn drawHair ( i32, color, color ) -> ( ):")
    emit("  locals: i32 i32 color")

    def hair_poly(vertices):
        for x, y in vertices:
            f32(repr(float(x)))
            f32(repr(float(y)))
        op("local.get", 1)
        op("poly", len(vertices))

    def style_case(style, body):
        emit("  local.get 0")
        i32(style)
        op("i32.eq")
        op("if")
        body()
        op("else")

    def end_cases():
        op("end")

    style_case(0, lambda: (emit("    ; Cropped"),
                           hair_poly([(19, 23), (20, 16), (25, 11), (37, 11), (44, 17),
                                       (44, 24), (39, 20), (35, 17), (26, 18)])))
    style_case(1, lambda: (emit("    ; Swept"),
                           hair_poly([(19, 27), (18, 18), (23, 11), (37, 9), (44, 15),
                                       (45, 24), (39, 20), (36, 16), (27, 23), (23, 23)])))
    style_case(2, lambda: emit_curls())
    style_case(3, lambda: (emit("    ; Bob"),
                           hair_poly([(18, 39), (18, 17), (23, 11), (38, 11), (45, 19),
                                       (46, 39), (41, 42), (40, 21), (33, 18), (25, 23),
                                       (23, 40)])))
    style_case(4, lambda: (emit("    ; Ponytail"),
                           hair_poly([(20, 26), (19, 17), (25, 11), (38, 12), (44, 19),
                                       (43, 28), (40, 20), (29, 17), (24, 23)]),
                           f32(45.0), f32(22.0), f32(4.0), f32(2.0),
                           op("local.get", 2), op("rect")))
    emit("  local.get 0")
    i32(5)
    op("i32.eq")
    op("if")
    emit("    ; Long")
    hair_poly([(16, 48), (18, 19), (24, 11), (38, 11), (44, 19), (48, 49),
               (41, 46), (39, 22), (33, 17), (25, 24), (23, 46)])
    op("else")
    emit("    ; ---- original styles (6-10) ----")
    style_case(6, lambda: emit_mohawk())
    style_case(7, lambda: emit_topknot())
    style_case(8, lambda: emit_afro())
    style_case(9, lambda: emit_pigtails())
    emit("    ; Side-sweep")
    hair_poly([(16, 20), (20, 8), (44, 7), (46, 22), (40, 26), (24, 28), (18, 26)])
    f32(24.0)
    f32(12.0)
    f32(30.0)
    f32(24.0)
    i32(1)
    op("local.get", 1)
    i32(14)
    op("color.shade")
    op("line")
    for _ in range(4):
        end_cases()  # close style_case(6-9)
    end_cases()  # close the Long if
    for _ in range(5):
        end_cases()  # close style_case(0-4)
    emit("  return")
    emit("endfn")
    emit("")


def emit_mohawk():
    emit("    ; Mohawk: a spiked crest, shaved sides")
    f32(28.0)
    f32(10.0)
    f32(8.0)
    f32(10.0)
    op("local.get", 1)
    op("rect")
    for x0, x1, tip, shade in [(24, 30, 3, 0), (28, 34, 2, 10), (32, 38, 4, 0), (26, 32, 6, -8)]:
        f32(repr(float(x0)))
        f32(14.0)
        f32(repr(float(x1)))
        f32(repr(float(tip)))
        f32(repr(float((x0 + x1) / 2)))
        f32(12.0)
        op("local.get", 1)
        if shade:
            i32(shade)
            op("color.shade")
        op("poly", 3)


def emit_topknot():
    emit("    ; Topknot: swept base, a bun and an accent tie")
    for x, y in [(20, 16), (22, 9), (42, 9), (44, 17), (40, 15), (24, 15)]:
        f32(repr(float(x)))
        f32(repr(float(y)))
    op("local.get", 1)
    op("poly", 6)
    f32(32.0)
    f32(5.0)
    f32(4.0)
    f32(4.0)
    op("local.get", 1)
    op("oval")
    f32(31.0)
    f32(4.0)
    f32(2.0)
    f32(2.0)
    op("local.get", 1)
    i32(14)
    op("color.shade")
    op("oval")
    f32(28.0)
    f32(8.0)
    f32(8.0)
    f32(2.0)
    op("local.get", 2)
    op("rect")


def emit_afro():
    emit("    ; Afro: a big textured cloud")
    f32(32.0)
    f32(13.0)
    f32(16.0)
    f32(14.0)
    op("local.get", 1)
    op("oval")
    for x, y, r, shade in [(25, 9, 4, -12), (39, 11, 4, -12), (32, 5, 4, 8),
                           (20, 16, 3, -8), (44, 16, 3, -8)]:
        f32(repr(float(x)))
        f32(repr(float(y)))
        f32(repr(float(r)))
        f32(repr(float(r)))
        op("local.get", 1)
        i32(shade)
        op("color.shade")
        op("oval")


def emit_pigtails():
    emit("    ; Pigtails: a cap and two buns with accent ties")
    for x, y in [(22, 16), (25, 9), (39, 9), (42, 16), (40, 14), (24, 14)]:
        f32(repr(float(x)))
        f32(repr(float(y)))
    op("local.get", 1)
    op("poly", 6)
    for x in (17, 47):
        f32(repr(float(x)))
        f32(19.0)
        f32(4.0)
        f32(4.0)
        op("local.get", 1)
        op("oval")
        f32(repr(float(x - 1)))
        f32(18.0)
        f32(2.0)
        f32(2.0)
        op("local.get", 1)
        i32(12)
        op("color.shade")
        op("oval")
        f32(repr(float(x - 3)))
        f32(18.0)
        f32(4.0)
        f32(2.0)
        op("local.get", 2)
        op("rect")


def emit_curls():
    emit("    ; Curls: a 3x5 grid of ovals; col = local 3, row = local 4,")
    emit("    ; colour temp = local 5")
    i32(0)
    op("local.set", 4)
    op("loop")
    i32(0)
    op("local.set", 3)
    op("loop")
    op("local.get", 3)
    op("i32.to.f32")
    f32(5.0)
    op("f32.mul")
    f32(21.0)
    op("f32.add")
    op("local.get", 4)
    op("i32.to.f32")
    f32(4.0)
    op("f32.mul")
    f32(14.0)
    op("f32.add")
    op("local.get", 3)
    i32(2)
    op("i32.eq")
    op("if")
    f32(3.0)
    op("f32.sub")
    op("end")
    f32(4.0)
    f32(4.0)
    op("local.get", 1)
    op("local.set", 5)
    op("local.get", 3)
    op("local.get", 4)
    op("i32.add")
    i32(2)
    op("i32.rem_s")
    i32(0)
    op("i32.eq")
    op("if")
    op("local.get", 1)
    i32(12)
    op("color.shade")
    op("local.set", 5)
    op("end")
    op("local.get", 5)
    op("oval")
    op("local.get", 3)
    i32(1)
    op("i32.add")
    op("local.set", 3)
    op("local.get", 3)
    i32(5)
    op("i32.lt_s")
    op("br_if", 0)
    op("end")
    op("local.get", 4)
    i32(1)
    op("i32.add")
    op("local.set", 4)
    op("local.get", 4)
    i32(3)
    op("i32.lt_s")
    op("br_if", 0)
    op("end")


# ---------------------------------------------------------------------------
# Vertex expressions: ("lit", v) | ("add"|"sub", const, "w"|"sh").
# ---------------------------------------------------------------------------
def emit_vertex(spec):
    kind = spec[0]
    if kind == "lit":
        f32(repr(float(spec[1])))
        return
    f32(repr(float(spec[1])))
    if spec[2] == "jaw":
        op("local.get", 24)
        op("i32.to.f32")
    else:
        op("local.get", 13 if spec[2] == "w" else 14)
    op("f32.add" if kind == "add" else "f32.sub")


def plain_poly(coords, color_code):
    # `coords` is a flat list of (x, y) coordinate specs; each vertex is a
    # pair.
    for spec in coords:
        emit_vertex(spec)
    color_code()
    op("poly", len(coords) // 2)


# ---------------------------------------------------------------------------
# The program.
# ---------------------------------------------------------------------------
emit("; 1:1 port of QuantSpace character portraits")
emit("; (PaCharacter::generate + PaPortrait::generate), generated by")
emit("; scripts/gen_portrait.py — do not edit by hand.")
emit("")
emit("meta: size=64x64 origin=32,60 variants=8 archetypes=1 fuel=2000000")
emit("output: point pupilCenter")
emit("")

emit_picker("pickSkin", SKIN)
emit_picker("pickHair", HAIR)
emit_picker("pickEyes", EYES)
emit_picker("pickClothing", CLOTHING)
emit_picker("pickAccent", ACCENT)
emit_age()
emit_hairstyles()

emit("fn draw ( i32, i32 ) -> ( ):")
emit("  locals: i32 i32 color color color color color i32 i32 i32 i32 f32 f32 i32 color i32 i32 f32 f32 f32 f32 i32 i32 i32")

# Locals: 0 variant, 1 archetype, 2 female, 3 age, 4 skin, 5 hair, 6 eyes,
# 7 clothing, 8 accent, 9 hairstyle, 10 faceWidth, 11 freckles, 12 beard,
# 13 w, 14 shoulder, 15 longHair, 16 brow, 17 x, 18 y, 19..22 f32 scratch.

emit("  ; ---- identity (PaCharacter::generate), RNG order preserved ----")
op("stream.state")
i32(0xc4a7ac7e)
op("i32.xor")
op("hash")
op("stream.push")
i32(2)
op("rng.index")
op("local.set", 2)
i32(16)
op("rng.index")
op("drop")
i32(16)
op("rng.index")
op("drop")
op("rng.unit")
op("call ageFromUnit")
op("local.set", 3)
i32(8)
op("rng.index")
op("call pickSkin")
op("local.set", 4)
i32(8)
op("rng.index")
op("call pickHair")
op("local.set", 5)
i32(5)
op("rng.index")
op("call pickEyes")
op("local.set", 6)
i32(6)
op("rng.index")
op("call pickClothing")
op("local.set", 7)
i32(2)
op("rng.index")
op("call pickAccent")
op("local.set", 8)
i32(3)
op("rng.index")
emit("  ; map the raw index to the HairStyle enum: females get Bob/Ponytail/")
emit("  ; Long (3..5), males get Cropped/Swept/Curls (0..2)")
op("local.get", 2)
op("if")
i32(3)
op("i32.add")
op("end")
op("local.set", 9)
i32(3)
op("rng.index")
op("local.set", 10)
i32(4)
op("rng.index")
i32(0)
op("i32.eq")
op("local.set", 11)
i32(4)
op("rng.index")
i32(0)
op("i32.eq")
op("local.get", 2)
i32(0)
op("i32.eq")
op("i32.and")
op("local.set", 12)

emit("  ; ---- original hairstyles: ~40% of characters get one instead of the")
emit("  ; game's style (extra draws; the base identity stays 1:1) ----")
i32(5)
op("rng.index")
i32(2)
op("i32.lt_s")
op("if")
i32(5)
op("rng.index")
i32(6)
op("i32.add")
op("local.set", 9)
op("end")

emit("  ; ---- head-shape variation (added AFTER the identity draws, so the")
emit("  ; base identity stays 1:1 with the game) ----")
i32(3)
op("rng.index")
op("local.set", 23)
emit("  ; jaw = shape - 1  (-1 narrow .. +1 wide), then a strong sex shift:")
emit("  ; females round the jaw by -2, males square it by +2 — the jaws of")
emit("  ; the two sexes never overlap")
op("local.get", 23)
i32(1)
op("i32.sub")
op("local.set", 24)
op("local.get", 2)
op("if")
op("local.get", 24)
i32(2)
op("i32.sub")
op("local.set", 24)
op("else")
op("local.get", 24)
i32(2)
op("i32.add")
op("local.set", 24)
op("end")
emit("  ; w = 10 + faceWidth + shape, then females narrow the face by one")
op("local.get", 10)
op("local.get", 23)
op("i32.add")
i32(10)
op("i32.add")
op("local.get", 2)
op("if")
i32(1)
op("i32.sub")
op("end")
op("i32.to.f32")
op("local.set", 13)

emit("  ; ---- derived values ----")
op("local.get", 10)
op("i32.to.f32")
f32(11.0)
op("f32.add")
op("local.set", 13)
f32(21.0)
op("local.set", 14)
op("local.get", 2)
i32(1)
op("i32.eq")
op("if")
f32(17.0)
op("local.set", 14)
op("end")
op("local.get", 9)
i32(3)
op("i32.eq")
op("local.get", 9)
i32(5)
op("i32.eq")
op("i32.or")
op("local.set", 15)
op("local.get", 5)
i32(-13)
op("color.shade")
op("local.set", 16)

emit("  ; ---- background halo ----")
i32(0)
op("local.set", 18)
op("loop")
i32(0)
op("local.set", 17)
op("loop")
emit("      ; pixel coords first (the colour lands on top, spec §9.8)")
op("local.get", 17)
op("local.get", 18)
op("local.get", 17)
op("i32.to.f32")
f32(30.0)
op("f32.sub")
op("local.set", 19)
op("local.get", 18)
op("i32.to.f32")
f32(25.0)
op("f32.sub")
op("local.set", 20)
op("local.get", 19)
op("local.get", 19)
op("f32.mul")
op("local.get", 20)
op("local.get", 20)
op("f32.mul")
op("f32.add")
op("f32.sqrt")
f32(45.0)
op("f32.div")
f32(1.0)
op("f32.sub")
op("f32.neg")
op("local.set", 21)
op("local.get", 21)
f32(0.0)
op("f32.lt")
op("if")
f32(0.0)
op("local.set", 21)
op("end")
color(BG_LOW)
color(BG_HIGH)
op("local.get", 21)
op("color.mix")
op("local.get", 17)
op("local.get", 18)
op("i32.add")
i32(4)
op("i32.rem_s")
i32(0)
op("i32.eq")
op("if")
i32(2)
op("color.shade")
op("end")
op("pixel")
op("local.get", 17)
i32(1)
op("i32.add")
op("local.set", 17)
op("local.get", 17)
i32(64)
op("i32.lt_s")
op("br_if", 0)
op("end")
op("local.get", 18)
i32(1)
op("i32.add")
op("local.set", 18)
op("local.get", 18)
i32(64)
op("i32.lt_s")
op("br_if", 0)
op("end")

emit("  ; ---- corner accents ----")

def plain_line(a, b, color_code):
    f32(repr(float(a[0])))
    f32(repr(float(a[1])))
    f32(repr(float(b[0])))
    f32(repr(float(b[1])))
    i32(1)
    color_code()
    op("line")


plain_line((5, 9), (5, 4), lambda: color(CORNER))
plain_line((5, 4), (12, 4), lambda: color(CORNER))
plain_line((58, 49), (58, 56), lambda: color(CORNER))
plain_line((58, 56), (52, 56), lambda: color(CORNER))

emit("  ; ---- long-hair backdrop and ponytail (behind the jacket) ----")
op("local.get", 15)
op("if")
plain_poly([("sub", 29, "w"), ("lit", 20), ("add", 35, "w"), ("lit", 20),
            ("add", 36, "w"), ("lit", 46), ("sub", 28, "w"), ("lit", 46)],
           lambda: (op("local.get", 5), i32(-16), op("color.shade")))
op("end")
op("local.get", 9)
i32(4)
op("i32.eq")
op("if")
f32(46.0)
f32(30.0)
f32(6.0)
f32(15.0)
op("local.get", 5)
i32(-12)
op("color.shade")
op("oval")
op("end")

emit("  ; ---- jacket, shoulders, collar ----")
plain_poly([("lit", 11), ("lit", 64), ("sub", 32, "sh"), ("lit", 53),
            ("lit", 25), ("lit", 48), ("lit", 39), ("lit", 48),
            ("add", 32, "sh"), ("lit", 53), ("lit", 56), ("lit", 64)],
           lambda: color(OUTLINE))
plain_poly([("lit", 13), ("lit", 64), ("sub", 33, "sh"), ("lit", 54),
            ("lit", 26), ("lit", 49), ("lit", 38), ("lit", 49),
            ("add", 31, "sh"), ("lit", 54), ("lit", 54), ("lit", 64)],
           lambda: op("local.get", 7))
plain_poly([("lit", 37), ("lit", 50), ("add", 31, "sh"), ("lit", 54),
            ("lit", 54), ("lit", 64), ("lit", 35), ("lit", 64)],
           lambda: (op("local.get", 7), i32(-19), op("color.shade")))

emit("  ; ---- neck ----")
f32(27.0)
f32(40.0)
f32(10.0)
f32(13.0)
op("local.get", 4)
i32(-32)
op("color.shade")
op("rect")
f32(28.0)
f32(41.0)
f32(6.0)
f32(10.0)
op("local.get", 4)
op("rect")

emit("  ; ---- collar ----")
plain_poly([("lit", 25), ("lit", 48), ("lit", 31), ("lit", 54),
            ("lit", 26), ("lit", 57), ("lit", 22), ("lit", 50)],
           lambda: (op("local.get", 7), i32(23), op("color.shade")))
plain_poly([("lit", 39), ("lit", 48), ("lit", 32), ("lit", 54),
            ("lit", 37), ("lit", 57), ("lit", 42), ("lit", 51)],
           lambda: (op("local.get", 7), i32(10), op("color.shade")))

emit("  ; ---- zipper and badges ----")
plain_line((31, 55), (31, 63), lambda: color(OUTLINE))
plain_line((32, 57), (32, 63), lambda: op("local.get", 8))
f32(18.0)
f32(57.0)
f32(5.0)
f32(2.0)
op("local.get", 8)
op("rect")
f32(42.0)
f32(58.0)
f32(5.0)
f32(3.0)
color(OUTLINE)
op("rect")
f32(43.0)
f32(58.0)
f32(3.0)
f32(1.0)
color(BADGELINE)
op("rect")

emit("  ; ---- outfit variation (extra draws; identity stays 1:1) ----")
i32(4)
op("rng.index")
op("local.set", 25)
emit("  ; outfit 1: a lighter chest stripe")
op("local.get", 25)
i32(1)
op("i32.eq")
op("if")
f32(26.0)
f32(55.0)
f32(12.0)
f32(2.0)
op("local.get", 7)
op("local.get", 7)
i32(20)
op("color.shade")
f32(0.35)
op("color.mix")
op("rect")
op("end")
emit("  ; outfit 2: two dark chest pockets")
op("local.get", 25)
i32(2)
op("i32.eq")
op("if")
f32(20.0)
f32(58.0)
f32(6.0)
f32(4.0)
op("local.get", 7)
i32(-25)
op("color.shade")
op("rect")
f32(38.0)
f32(58.0)
f32(6.0)
f32(4.0)
op("local.get", 7)
i32(-25)
op("color.shade")
op("rect")
op("end")
emit("  ; outfit 3: accent lapels on the collar")
op("local.get", 25)
i32(3)
op("i32.eq")
op("if")
plain_poly([("lit", 25), ("lit", 48), ("lit", 31), ("lit", 54),
            ("lit", 28), ("lit", 52), ("lit", 24), ("lit", 49)],
           lambda: op("local.get", 8))
plain_poly([("lit", 39), ("lit", 48), ("lit", 32), ("lit", 54),
            ("lit", 36), ("lit", 52), ("lit", 40), ("lit", 49)],
           lambda: op("local.get", 8))
op("end")

emit("  ; ---- ears ----")
f32(32.0)
op("local.get", 13)
op("f32.sub")
f32(29.0)
f32(3.0)
f32(5.0)
op("local.get", 4)
i32(-32)
op("color.shade")
op("oval")
f32(32.0)
op("local.get", 13)
op("f32.add")
f32(29.0)
f32(3.0)
f32(5.0)
op("local.get", 4)
i32(-32)
op("color.shade")
op("oval")

emit("  ; ---- face planes ----")
plain_poly([("sub", 32, "w"), ("lit", 17), ("lit", 26), ("lit", 12),
            ("lit", 38), ("lit", 12), ("add", 32, "w"), ("lit", 19),
            ("add", 31, "w"), ("lit", 35), ("add", 38, "jaw"), ("lit", 43),
            ("lit", 30), ("lit", 46), ("sub", 23, "jaw"), ("lit", 40),
            ("sub", 32, "w"), ("lit", 31)],
           lambda: color(OUTLINE))
plain_poly([("sub", 33, "w"), ("lit", 18), ("lit", 27), ("lit", 14),
            ("lit", 37), ("lit", 14), ("add", 31, "w"), ("lit", 20),
            ("add", 31, "w"), ("lit", 34), ("add", 37, "jaw"), ("lit", 41),
            ("lit", 30), ("lit", 44), ("sub", 24, "jaw"), ("lit", 39),
            ("sub", 33, "w"), ("lit", 30)],
           lambda: op("local.get", 4))
plain_poly([("lit", 37), ("lit", 16), ("add", 31, "w"), ("lit", 20),
            ("add", 31, "w"), ("lit", 34), ("add", 37, "jaw"), ("lit", 41),
            ("lit", 31), ("lit", 44), ("lit", 35), ("lit", 36)],
           lambda: (op("local.get", 4), i32(-32), op("color.shade")))
plain_poly([("lit", 24), ("lit", 21), ("lit", 29), ("lit", 17),
            ("lit", 32), ("lit", 18), ("lit", 29), ("lit", 26),
            ("lit", 24), ("lit", 28)],
           lambda: (op("local.get", 4), i32(17), op("color.shade")))
f32(24.0)
f32(31.0)
f32(5.0)
f32(3.0)
op("local.get", 4)
i32(17)
op("color.shade")
op("rect")
f32(35.0)
f32(31.0)
f32(5.0)
f32(3.0)
op("local.get", 4)
op("local.get", 4)
i32(-32)
op("color.shade")
f32(0.4)
op("color.mix")
op("rect")

emit("  ; ---- brows, eyes, nose, mouth ----")
plain_line((23, 25), (28, 24), lambda: op("local.get", 16))
plain_line((35, 24), (40, 25), lambda: op("local.get", 16))
f32(23.0)
f32(27.0)
f32(6.0)
f32(2.0)
color(EYEWHITE)
op("rect")
f32(35.0)
f32(27.0)
f32(6.0)
f32(2.0)
color(EYEWHITE)
op("rect")
f32(26.0)
f32(27.0)
f32(2.0)
f32(2.0)
op("local.get", 6)
op("rect")
f32(36.0)
f32(27.0)
f32(2.0)
f32(2.0)
op("local.get", 6)
op("rect")
i32(27)
i32(27)
color(OUTLINE)
op("pixel")
i32(37)
i32(27)
color(OUTLINE)
op("pixel")
i32(25)
i32(27)
color(GLINT)
op("pixel")
i32(35)
i32(27)
color(GLINT)
op("pixel")
plain_line((32, 27), (31, 33), lambda: (op("local.get", 4), i32(-32), op("color.shade")))
plain_line((31, 33), (34, 34), lambda: (op("local.get", 4), i32(-46), op("color.shade")))
i32(31)
i32(31)
op("local.get", 4)
i32(17)
op("color.shade")
op("pixel")
plain_line((28, 37), (35, 37), lambda: (op("local.get", 4), color(MOUTHMIX), f32(0.52), op("color.mix")))
plain_line((29, 39), (34, 39), lambda: (op("local.get", 4), i32(17), op("color.shade")))

emit("  ; ---- beard (male only) ----")
op("local.get", 12)
op("if")
plain_poly([("lit", 24), ("lit", 34), ("lit", 27), ("lit", 38),
            ("lit", 32), ("lit", 40), ("lit", 38), ("lit", 35),
            ("lit", 38), ("lit", 40), ("lit", 32), ("lit", 44),
            ("lit", 26), ("lit", 40)],
           lambda: (op("local.get", 5), op("local.get", 4), f32(0.22), op("color.mix")))
plain_line((28, 37), (34, 37), lambda: (op("local.get", 5), i32(-18), op("color.shade")))
op("end")

emit("  ; ---- freckles ----")
op("local.get", 11)
op("if")
for x, y in [(24, 32), (27, 33), (37, 33), (40, 32)]:
    i32(x)
    i32(y)
    op("local.get", 4)
    i32(-47)
    op("color.shade")
    op("pixel")
op("end")

emit("  ; ---- hairstyle ----")
op("local.get", 9)
op("local.get", 5)
op("local.get", 8)
op("call drawHair")

emit("  ; ---- hair highlights ----")
f32(24.0)
f32(15.0)
f32(30.0)
f32(13.0)
i32(2)
op("local.get", 5)
i32(22)
op("color.shade")
op("line")

op("local.get", 15)
op("if")
plain_line((20, 28), (19, 42), lambda: (op("local.get", 5), i32(17), op("color.shade")))
plain_line((43, 30), (44, 41), lambda: (op("local.get", 5), i32(-18), op("color.shade")))
op("end")

emit("  ; ---- the pupil anchor (extra metadata; does not affect pixels) ----")
f32(27.0)
f32(28.0)
op("out.point", "pupilCenter")
emit("  return")
emit("endfn")

text = "\n".join(out) + "\n"
sys.stdout.write(text)

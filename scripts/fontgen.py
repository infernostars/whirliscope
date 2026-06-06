import argparse
import json
import re
from pathlib import Path


DEFAULT_FIRST_CODEPOINT = 32
DEFAULT_LAST_CODEPOINT = 128
DEFAULT_SYMBOL_PREFIX = "WS_FONT"
DEFAULT_ARRAY_NAME = "ws_font"
MAX_PACKED_WIDTH = 8


def sanitize_identifier(value):
    value = re.sub(r"[^A-Za-z0-9_]", "_", value)
    if not value or value[0].isdigit():
        value = f"_{value}"
    return value


def load_characters(path):
    data = json.loads(path.read_text())
    if not isinstance(data, list):
        raise ValueError("font JSON must contain a list of character records")
    return data


def selected_characters(characters, first_codepoint, last_codepoint):
    selected = []
    for character in characters:
        codepoint = character.get("codepoint")
        pixels = character.get("pixels")
        if not isinstance(codepoint, int):
            continue
        if first_codepoint <= codepoint <= last_codepoint:
            if not isinstance(pixels, list):
                raise ValueError(f"codepoint {codepoint}: pixels must be a list")
            selected.append(character)

    if not selected:
        raise ValueError(
            f"no glyphs found in requested range {first_codepoint}-{last_codepoint}"
        )
    return selected


def infer_dimensions(selected):
    font_width = 0
    font_height = 0

    for character in selected:
        codepoint = character["codepoint"]
        pixels = character["pixels"]
        font_height = max(font_height, len(pixels))

        for row in pixels:
            if not isinstance(row, list):
                raise ValueError(f"codepoint {codepoint}: each pixel row must be a list")
            font_width = max(font_width, len(row))

    if font_width == 0 or font_height == 0:
        raise ValueError("font glyphs must contain at least one pixel")
    if font_width > MAX_PACKED_WIDTH:
        raise ValueError(
            f"font width {font_width} exceeds packed uint8_t limit "
            f"of {MAX_PACKED_WIDTH}"
        )

    return font_width, font_height


def glyph_row_to_byte(row, font_width, codepoint):
    if len(row) > font_width:
        raise ValueError(
            f"codepoint {codepoint}: expected row width at most {font_width}, "
            f"got {len(row)}"
        )

    value = 0
    for index, pixel in enumerate(row):
        if pixel not in (0, 1):
            raise ValueError(
                f"codepoint {codepoint}: expected pixel value 0 or 1, got {pixel!r}"
            )
        if pixel:
            value |= 1 << (font_width - 1 - index)
    return value


def build_glyphs(selected, first_codepoint, last_codepoint, font_width, font_height):
    glyphs = {}
    for character in selected:
        codepoint = character["codepoint"]
        pixels = character["pixels"]

        rows = [glyph_row_to_byte(row, font_width, codepoint) for row in pixels]

        # Pad below source rows. This keeps descenders and any other glyphs that
        # use lower rows within the inferred global font height instead of
        # normalizing or cropping them.
        rows.extend([0] * (font_height - len(rows)))
        glyphs[codepoint] = rows

    fallback = glyphs.get(ord("?"))
    if fallback is None:
        fallback = [0] * font_height

    for codepoint in range(first_codepoint, last_codepoint + 1):
        if codepoint not in glyphs:
            glyphs[codepoint] = fallback

    return glyphs


def write_header(output_path, input_path, glyphs, first_codepoint, last_codepoint,
                 font_width, font_height, symbol_prefix, array_name):
    guard = sanitize_identifier(
        f"WHIRLISCOPE_GENERATED_{symbol_prefix}_H"
    ).upper()
    symbol_prefix = sanitize_identifier(symbol_prefix).upper()
    array_name = sanitize_identifier(array_name)

    lines = [
        f"/* Generated from {input_path}. Do not edit. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        f"#define {symbol_prefix}_FIRST {first_codepoint}u",
        f"#define {symbol_prefix}_LAST {last_codepoint}u",
        f"#define {symbol_prefix}_WIDTH {font_width}u",
        f"#define {symbol_prefix}_HEIGHT {font_height}u",
        f"#define {symbol_prefix}_COUNT "
        f"({symbol_prefix}_LAST - {symbol_prefix}_FIRST + 1u)",
        "",
        f"static const uint8_t {array_name}"
        f"[{symbol_prefix}_COUNT][{symbol_prefix}_HEIGHT] = {{",
    ]

    for codepoint in range(first_codepoint, last_codepoint + 1):
        escaped = chr(codepoint).encode("unicode_escape").decode("ascii")
        rows = ", ".join(f"0x{row:02x}" for row in glyphs[codepoint])
        lines.append(
            f"    [{codepoint}u - {symbol_prefix}_FIRST] = "
            f"{{{rows}}}, /* {escaped} */"
        )

    lines.extend([
        "};",
        "",
        f"#endif /* {guard} */",
        "",
    ])

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(lines))


def build_parser():
    parser = argparse.ArgumentParser(
        description="Generate a packed C bitmap font header from character JSON."
    )
    parser.add_argument("input_json", type=Path)
    parser.add_argument("output_header", type=Path)
    parser.add_argument("--first", type=int, default=DEFAULT_FIRST_CODEPOINT)
    parser.add_argument("--last", type=int, default=DEFAULT_LAST_CODEPOINT)
    parser.add_argument("--symbol-prefix", default=DEFAULT_SYMBOL_PREFIX)
    parser.add_argument("--array-name", default=DEFAULT_ARRAY_NAME)
    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.first > args.last:
        parser.error("--first must be less than or equal to --last")

    characters = load_characters(args.input_json)
    selected = selected_characters(characters, args.first, args.last)
    font_width, font_height = infer_dimensions(selected)
    glyphs = build_glyphs(
        selected, args.first, args.last, font_width, font_height
    )
    write_header(
        args.output_header,
        args.input_json,
        glyphs,
        args.first,
        args.last,
        font_width,
        font_height,
        args.symbol_prefix,
        args.array_name,
    )
    return 0

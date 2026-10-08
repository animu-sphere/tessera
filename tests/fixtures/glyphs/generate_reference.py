"""Regenerate small grayscale references directly through FreeType, without Tessera.

Usage: python generate_reference.py <path-to-freetype-library>
Requires FreeType 2.13.3 and the committed sibling fonts. No Python packages needed.
Review changed images/bearings and record new dependency evidence before accepting them.
"""

import ctypes as c
import json
from pathlib import Path
import sys


class Generic(c.Structure):
    _fields_ = [("data", c.c_void_p), ("finalizer", c.c_void_p)]


class Vector(c.Structure):
    _fields_ = [("x", c.c_long), ("y", c.c_long)]


class BBox(c.Structure):
    _fields_ = [(name, c.c_long) for name in ("xMin", "yMin", "xMax", "yMax")]


class Bitmap(c.Structure):
    _fields_ = [("rows", c.c_uint), ("width", c.c_uint), ("pitch", c.c_int),
                ("buffer", c.c_void_p), ("num_grays", c.c_ushort),
                ("pixel_mode", c.c_ubyte), ("palette_mode", c.c_ubyte), ("palette", c.c_void_p)]


class Slot(c.Structure):
    _fields_ = [("library", c.c_void_p), ("face", c.c_void_p), ("next", c.c_void_p),
                ("glyph_index", c.c_uint), ("generic", Generic), ("metrics", c.c_long * 8),
                ("linearHoriAdvance", c.c_long), ("linearVertAdvance", c.c_long),
                ("advance", Vector), ("format", c.c_uint), ("bitmap", Bitmap),
                ("bitmap_left", c.c_int), ("bitmap_top", c.c_int)]


class Face(c.Structure):
    _fields_ = [(name, c.c_long) for name in ("num_faces", "face_index", "face_flags", "style_flags", "num_glyphs")] + [
        ("family_name", c.c_char_p), ("style_name", c.c_char_p), ("num_fixed_sizes", c.c_int),
        ("available_sizes", c.c_void_p), ("num_charmaps", c.c_int), ("charmaps", c.c_void_p),
        ("generic", Generic), ("bbox", BBox), ("units_per_EM", c.c_ushort)] + [
        (name, c.c_short) for name in ("ascender", "descender", "height", "max_advance_width",
                                      "max_advance_height", "underline_position", "underline_thickness")] + [
        ("glyph", c.POINTER(Slot))]


class Parameter(c.Structure):
    _fields_ = [("tag", c.c_ulong), ("data", c.c_void_p)]


def main():
    ft = c.CDLL(sys.argv[1])
    signatures = {
        "FT_Init_FreeType": [c.POINTER(c.c_void_p)],
        "FT_Library_Version": [c.c_void_p, c.POINTER(c.c_int), c.POINTER(c.c_int), c.POINTER(c.c_int)],
        "FT_New_Face": [c.c_void_p, c.c_char_p, c.c_long, c.POINTER(c.POINTER(Face))],
        "FT_Set_Char_Size": [c.POINTER(Face), c.c_long, c.c_long, c.c_uint, c.c_uint],
        "FT_Face_Properties": [c.POINTER(Face), c.c_uint, c.POINTER(Parameter)],
        "FT_Load_Glyph": [c.POINTER(Face), c.c_uint, c.c_int32],
        "FT_Render_Glyph": [c.POINTER(Slot), c.c_int],
        "FT_Done_Face": [c.POINTER(Face)],
        "FT_Done_FreeType": [c.c_void_p],
    }
    for name, args in signatures.items():
        getattr(ft, name).argtypes = args
        getattr(ft, name).restype = None if name == "FT_Library_Version" else c.c_int

    def ok(error):
        if error:
            raise RuntimeError(f"FreeType error {error}")

    library = c.c_void_p()
    ok(ft.FT_Init_FreeType(c.byref(library)))
    try:
        version = [c.c_int() for _ in range(3)]
        ft.FT_Library_Version(library, *(c.byref(v) for v in version))
        if tuple(v.value for v in version) != (2, 13, 3):
            raise RuntimeError("References require FreeType 2.13.3")
        root = Path(__file__).resolve().parent
        cases = [
            ("latin-A-20", "NotoSans-Regular.ttf", 36, 1280),
            ("latin-j-20", "NotoSans-Regular.ttf", 77, 1280),
            ("latin-ffi-20", "NotoSans-Regular.ttf", 1656, 1280),
            ("latin-mark-20", "NotoSans-Regular.ttf", 2665, 1280),
            ("latin-A-20.25", "NotoSans-Regular.ttf", 36, 1296),
            ("japanese-me-25", "NotoSansJP-Regular.otf", 1362, 1600),
            ("latin-notdef-20", "NotoSans-Regular.ttf", 0, 1280),
        ]
        manifest = []
        for name, font, glyph, size in cases:
            face = c.POINTER(Face)()
            ok(ft.FT_New_Face(library, str(root.parent / "fonts" / font).encode(), 0, c.byref(face)))
            try:
                darkening = c.c_ubyte(0)
                # FT_PARAM_TAG_STEM_DARKENING = FT_MAKE_TAG('d','a','r','k').
                parameter = Parameter(0x6461726B, c.cast(c.byref(darkening), c.c_void_p))
                ok(ft.FT_Face_Properties(face, 1, c.byref(parameter)))
                ok(ft.FT_Set_Char_Size(face, 0, size, 72, 72))
                ok(ft.FT_Load_Glyph(face, glyph, (1 << 1) | (1 << 3) | (1 << 15)))
                ok(ft.FT_Render_Glyph(face.contents.glyph, 0))
                slot = face.contents.glyph.contents
                bitmap = slot.bitmap
                assert bitmap.pixel_mode == 2 and bitmap.num_grays == 256 and bitmap.width and bitmap.rows
                coverage = b"".join(c.string_at(bitmap.buffer + row * bitmap.pitch, bitmap.width)
                                    for row in range(bitmap.rows))
                (root / (name + ".pgm")).write_bytes(f"P5\n{bitmap.width} {bitmap.rows}\n255\n".encode() + coverage)
                manifest.append(dict(name=name, font=font, glyph=glyph, pixel_size_64=size,
                                     width=bitmap.width, height=bitmap.rows, left=slot.bitmap_left, top=slot.bitmap_top))
            finally:
                ok(ft.FT_Done_Face(face))
        (root / "reference.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(manifest, indent=2))
    finally:
        ok(ft.FT_Done_FreeType(library))


if __name__ == "__main__":
    main()

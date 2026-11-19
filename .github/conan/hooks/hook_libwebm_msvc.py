"""Let MSVC compile libwebm's private HasTag trait.

ChildParser::has_tag() instantiates HasTag on another MasterValueParser
specialization. MSVC 14.44 and 14.51 reject that as C2248 because the trait
is private. HasTag only inspects its own parameter pack, so making it public
does not change the parsed result.
"""

import os

_NEEDLE_PUBLIC = (
    "  // Helper struct that will be std::true_type if Tag is in Tags, or\n"
    "  // std::false_type otherwise.\n"
    "  template <typename Tag, typename... Tags>\n"
    "  struct HasTag;"
)

_REPL_PUBLIC = (
    " public:\n"
    "  // Helper struct that will be std::true_type if Tag is in Tags, or\n"
    "  // std::false_type otherwise.\n"
    "  // Published so MSVC can instantiate the trait from another\n"
    "  // MasterValueParser specialization (C2248).\n"
    "  template <typename Tag, typename... Tags>\n"
    "  struct HasTag;"
)

_NEEDLE_PRIVATE = (
    "  struct HasTag<Tag, Tag, Tags...> : std::true_type {};\n"
    "\n"
    "  template <typename Base, typename F, typename... Tags>\n"
    "  class ChildParser : public Base {"
)

_REPL_PRIVATE = (
    "  struct HasTag<Tag, Tag, Tags...> : std::true_type {};\n"
    "\n"
    " private:\n"
    "  template <typename Base, typename F, typename... Tags>\n"
    "  class ChildParser : public Base {"
)

_MARKER = "Published so MSVC can instantiate the trait"


def _header_paths(conanfile):
    folders = []
    for attr in ("source_folder", "build_folder"):
        folder = getattr(conanfile, attr, None)
        if folder and folder not in folders:
            folders.append(folder)
    rels = (
        os.path.join("webm_parser", "src", "master_value_parser.h"),
        os.path.join("src", "webm_parser", "src", "master_value_parser.h"),
    )
    paths = []
    for folder in folders:
        for rel in rels:
            path = os.path.normpath(os.path.join(folder, rel))
            if path not in paths:
                paths.append(path)
    return paths


def _patch(conanfile, required):
    if getattr(conanfile, "name", None) != "libwebm":
        return

    patched = False
    for path in _header_paths(conanfile):
        if not os.path.isfile(path):
            continue
        with open(path, "r", encoding="utf-8", newline="") as handle:
            text = handle.read().replace("\r\n", "\n")
        if _MARKER in text:
            conanfile.output.info(f"libwebm MSVC patch already applied: {path}")
            patched = True
            continue
        if _NEEDLE_PUBLIC not in text or _NEEDLE_PRIVATE not in text:
            raise RuntimeError(
                f"libwebm header layout changed, cannot patch {path}"
            )
        text = text.replace(_NEEDLE_PUBLIC, _REPL_PUBLIC, 1)
        text = text.replace(_NEEDLE_PRIVATE, _REPL_PRIVATE, 1)
        with open(path, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        conanfile.output.info(f"Patched libwebm for MSVC HasTag access: {path}")
        patched = True

    if required and not patched:
        raise RuntimeError(
            "libwebm is about to build, but master_value_parser.h was not found"
        )


_PC_MARKER = "Alpharino: keep Conan's generated .pc files visible"


def _pin_ffmpeg_pkgconfig(conanfile):
    """The MSYS login shell used to configure FFmpeg rewrites PKG_CONFIG_PATH.

    Windows pkgconf then cannot see dav1d.pc (and the other Conan .pc files)
    in the generators folder, so configure stops with
    "dav1d >= 0.5.0 not found using pkg-config".
    """
    if getattr(conanfile, "name", None) != "ffmpeg":
        return
    generators = getattr(conanfile, "generators_folder", None)
    if not generators or not os.path.isdir(generators):
        raise RuntimeError("ffmpeg generators folder is missing; cannot set PKG_CONFIG_PATH")

    pc_dir = generators.replace("\\", "/")
    snippet = (
        "\n# Alpharino: keep Conan's generated .pc files visible to Windows pkgconf.\n"
        f'_alpharino_pc="{pc_dir}"\n'
        "if command -v cygpath >/dev/null 2>&1; then\n"
        '  _alpharino_pc="$(cygpath -m "$_alpharino_pc")"\n'
        "fi\n"
        'export PKG_CONFIG_PATH="$_alpharino_pc"\n'
    )
    patched = False
    for name in os.listdir(generators):
        if not name.endswith(".sh"):
            continue
        path = os.path.join(generators, name)
        if not os.path.isfile(path):
            continue
        with open(path, "r", encoding="utf-8", newline="") as handle:
            text = handle.read().replace("\r\n", "\n")
        if _PC_MARKER in text:
            patched = True
            continue
        with open(path, "a", encoding="utf-8", newline="\n") as handle:
            handle.write(snippet)
        conanfile.output.info(f"Pinned FFmpeg PKG_CONFIG_PATH in {path}")
        patched = True
    if not patched:
        raise RuntimeError(f"no shell env scripts found in {generators}")


def post_source(conanfile):
    _patch(conanfile, required=False)


def pre_build(conanfile):
    _patch(conanfile, required=True)
    _pin_ffmpeg_pkgconfig(conanfile)

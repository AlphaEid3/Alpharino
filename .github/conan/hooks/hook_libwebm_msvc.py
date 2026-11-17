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


def post_source(conanfile):
    _patch(conanfile, required=False)


def pre_build(conanfile):
    _patch(conanfile, required=True)

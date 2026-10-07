# PlatformIO compatibility bridge for esphome/micro-vorbis under WLED/Arduino.
#
# Upstream micro-vorbis is packaged as an ESP-IDF/CMake component. PlatformIO's
# generic Arduino library builder reaches src/ and src/tremor, but it does not
# import the component's private include directories and it also does not build
# the nested lib/micro-ogg-demuxer subproject that CMake links separately.
#
# This script therefore does two integration-only jobs:
#   1) export the dependency's private/header namespace paths through CPPPATH;
#   2) compile the bundled micro-ogg-demuxer src/ tree into the same firmware.
#
# No downloaded dependency source is patched or copied, and WLED remains on the
# Arduino framework.

Import("env")

from pathlib import Path


def _is_under(path: Path, root: Path) -> bool:
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def _collect_header_paths(root: Path):
    paths = set()

    # Known upstream roots are added even if a clean build has not populated
    # every nested directory yet when this script runs.
    for rel in (
        "include",
        "src",
        "src/tremor",
        "lib",
        "lib/micro-ogg-demuxer/include",
        "lib/micro-ogg-demuxer/src",
        "lib/ogg/include",
        "lib/libogg/include",
    ):
        paths.add(root / rel)

    # The PlatformIO registry archive can evolve independently of the ESP-IDF
    # CMake layout. Discover actual header locations rather than hard-coding a
    # single libogg/micro-ogg path. For a header .../include/ogg/ogg.h we add
    # both .../include/ogg and .../include, so <ogg/ogg.h> resolves. For
    # src/tremor/ivorbiscodec.h we add src/tremor directly.
    if root.is_dir():
        for header in root.rglob("*.h"):
            parent = header.parent
            paths.add(parent)
            if parent != root and _is_under(parent, root):
                paths.add(parent.parent)

    return sorted(str(p) for p in paths if _is_under(p, root))


def _build_micro_ogg(root: Path):
    # micro-vorbis uses microOggDemuxer as a nested CMake subproject. The
    # PlatformIO Arduino library builder compiles micro-vorbis/src recursively
    # but does not descend into lib/, which leaves OggDemuxer methods undefined
    # at final link. Build exactly the bundled demuxer's src tree, preserving the
    # version shipped with the selected micro-vorbis package.
    micro_ogg_src = root / "lib" / "micro-ogg-demuxer" / "src"
    if not micro_ogg_src.is_dir():
        raise RuntimeError(
            "micro-vorbis package is missing lib/micro-ogg-demuxer/src; "
            "cannot satisfy OggDemuxer link dependency"
        )

    sources = sorted(
        [*micro_ogg_src.rglob("*.cpp"), *micro_ogg_src.rglob("*.c")]
    )
    if not sources:
        raise RuntimeError(
            "micro-ogg-demuxer src directory contains no compilable sources"
        )

    env.BuildSources(
        str(Path(env.subst("$BUILD_DIR")) / "micro-ogg-demuxer"),
        str(micro_ogg_src),
    )
    return micro_ogg_src, len(sources)


pioenv = env.subst("$PIOENV")
libdeps_base = Path(env.subst("$PROJECT_LIBDEPS_DIR"))
root = libdeps_base / pioenv / "micro-vorbis"
include_paths = _collect_header_paths(root)

# CPPPATH is propagated to C and C++ compilation, including dependency library
# builders derived from this environment.
env.AppendUnique(CPPPATH=include_paths)

micro_ogg_src, micro_ogg_sources = _build_micro_ogg(root)

print("[spotify/micro-vorbis] include bridge: root=%s paths=%d" % (root, len(include_paths)))
print("[spotify/micro-vorbis] micro-ogg source bridge: src=%s files=%d" %
      (micro_ogg_src, micro_ogg_sources))

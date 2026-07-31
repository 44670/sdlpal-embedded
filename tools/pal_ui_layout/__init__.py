"""Deterministic build-time layout compiler for small SDLPAL displays.

The package deliberately contains no target runtime code.  It turns display,
font, asset, and semantic screen descriptions into integer layout decisions
that can be emitted as ``static const`` C data and verified on the host.
"""

from .profiles import DisplayProfile, certified_profiles, parse_resolution

__all__ = [
    "DisplayProfile",
    "certified_profiles",
    "parse_resolution",
]

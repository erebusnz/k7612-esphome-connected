"""Locate the compiler and simulator used by hardware-independent firmware tests."""
import os
from pathlib import Path
import shutil


def tool(name):
    """Prefer an explicit toolchain, then PlatformIO, then the host PATH."""
    filename = name + ('.exe' if os.name == 'nt' else '')
    override = os.environ.get('SDCC_BIN')
    core = Path(os.environ.get('PLATFORMIO_CORE_DIR', Path.home() / '.platformio'))
    directory = Path(override) if override else core / 'packages/toolchain-sdcc/bin'
    candidate = directory / filename
    if candidate.is_file():
        return candidate
    if not override:
        found = shutil.which(filename)
        if found:
            return Path(found)
    raise FileNotFoundError(
        f'Cannot find {filename}. Build with PlatformIO first, or install SDCC '
        'and its s51 simulator and set SDCC_BIN to their executable directory.'
    )


def compiler_environment():
    directories = dict.fromkeys(str(tool(name).parent) for name in ('sdcc', 's51'))
    return dict(os.environ, PATH=os.pathsep.join([*directories, os.environ.get('PATH', '')]))

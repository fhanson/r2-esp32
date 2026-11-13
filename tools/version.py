"""PlatformIO pre-build script: stamps the firmware version into every build.

Reads VERSION (one line, "major.minor.patch"), finds the git commit if there
is one, and passes them in as FW_VERSION, FW_BUILD and FW_NODE (see
lib/DroidCore/src/Version.h). After a build it copies the image to
dist/r2au-<env>-v<version>.bin, the file to use for an OTA update.
"""
import re
import shutil
import subprocess
from pathlib import Path

Import("env")  # noqa: F821  (PlatformIO)

ROOT = Path(env["PROJECT_DIR"])  # noqa: F821
NODE = env["PIOENV"]  # noqa: F821

version = (ROOT / "VERSION").read_text().strip()
if not re.fullmatch(r"\d+\.\d+\.\d+(-[0-9A-Za-z.]+)?", version) or len(version) > 23:
    raise SystemExit(f"VERSION must look like 1.2.3 or 1.2.3-dev, not {version!r}")


def git(*args):
    try:
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, timeout=5,
                              check=True).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None


commit = git("rev-parse", "--short=7", "HEAD")
build = commit or "nogit"
if commit and git("status", "--porcelain", "--untracked-files=no"):
    build += "-dirty"  # built from changes not yet committed

env.Append(CPPDEFINES=[  # noqa: F821
    ("FW_VERSION", env.StringifyMacro(version)),  # noqa: F821
    ("FW_BUILD", env.StringifyMacro(build)),  # noqa: F821
    ("FW_NODE", env.StringifyMacro(NODE)),  # noqa: F821
])
print(f"version: {NODE} v{version} ({build})")


def copy_release(source, target, env):  # noqa: ARG001
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    out = dist / f"r2au-{NODE}-v{version}.bin"
    shutil.copyfile(str(target[0]), out)
    print(f"version: OTA image {out.relative_to(ROOT)}")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_release)  # noqa: F821

# PlatformIO pre-script: the git short hash, for LMYC_FW_VERSION (boat_config.h).
# "-dirty" marks a build with uncommitted changes; "nogit" a build outside git.
import subprocess

Import("env")  # noqa: F821


def _git(*args):
    return subprocess.run(
        ["git", *args], cwd=env["PROJECT_DIR"], capture_output=True, text=True, check=True  # noqa: F821
    ).stdout.strip()


try:
    rev = _git("rev-parse", "--short=7", "HEAD")
    if _git("status", "--porcelain", "--untracked-files=no"):
        rev += "-dirty"
except Exception:
    rev = "nogit"

env.Append(CPPDEFINES=[("LMYC_FW_GIT", '\\"%s\\"' % rev)])  # noqa: F821
print("LMYC firmware git revision: %s" % rev)

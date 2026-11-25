# PlatformIO pre-build script: puts the git commit and the board name in the
# firmware (logged at start-up), so a log shows exactly which build is running.
import subprocess

Import("env")  # noqa: F821  (SCons)

try:
    rev = subprocess.run(["git", "describe", "--always", "--dirty"], capture_output=True, text=True,
                         cwd=env.subst("$PROJECT_DIR"), check=True).stdout.strip()  # noqa: F821
except Exception:
    rev = "unknown"
env.Append(CPPDEFINES=[("MANGLE_GIT", env.StringifyMacro(rev)),  # noqa: F821
                        ("MANGLE_BOARD", env.StringifyMacro(env.subst("$PIOENV")))])  # noqa: F821

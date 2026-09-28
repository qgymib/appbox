#!/usr/bin/env python3
"""Format the C++ sources of the appbox repository with clang-format.

The script runs on Windows only. It locates a clang-format executable, walks
the repository for every ``.hpp`` and ``.cpp`` file below the root and hands
them to clang-format. The submodule directory ``third_party`` and the
generated or tool owned directories are skipped, and the style comes from the
``.clang-format`` file at the root of the repository.

clang-format is searched in this order, and the first candidate which answers
``--version`` is used:

1. the ``clang-format`` on the ``PATH``,
2. the ``clang-format`` of every Visual Studio installation which
   ``vswhere.exe`` reports, below
   ``<installation>\\VC\\Tools\\Llvm\\<arch>\\bin\\clang-format.exe``,
3. the well known install locations, which are the Visual Studio directories
   of ``%ProgramFiles%`` and ``%ProgramFiles(x86)%`` and the standalone LLVM
   directories ``%ProgramFiles%\\LLVM\\bin``, ``%ProgramFiles(x86)%\\LLVM\\bin``
   and ``%LOCALAPPDATA%\\Programs\\LLVM\\bin``.

Usage:
    python tools/format.py
    python tools/format.py --check
    python tools/format.py --check --verbose
    python tools/format.py -j 4 --timeout 60

Exit codes:
    0   every file was processed successfully; in ``--check`` every file is
        formatted already,
    1   at least one file failed, or ``--check`` found a file which is not
        formatted,
    2   the environment or the command line is unusable, for example when no
        clang-format could be located or when the repository root does not
        hold the ``.clang-format`` file.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from collections.abc import Iterable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

# Directories which are never walked. The names are matched case insensitively
# and the prefixes cover the build directories of the build system, the CMake
# presets and the IDE generated trees.
EXCLUDED_DIR_NAMES = frozenset({"third_party", "build", ".git", ".vs", ".idea", ".codebuddy", ".vscode"})
EXCLUDED_DIR_PREFIXES = ("build", "cmake-build-", "out")

# Suffixes of the files which are formatted, and the style file which has to
# exist at the root of the repository.
SOURCE_SUFFIXES = frozenset({".hpp", ".cpp"})
STYLE_FILE_NAME = ".clang-format"

# Layout of a Visual Studio installation and of a standalone LLVM install.
VSWHERE_RELATIVE_PATH = Path("Microsoft Visual Studio") / "Installer" / "vswhere.exe"
VS_LLVM_RELATIVE_PATH = Path("VC") / "Tools" / "Llvm"
VS_LLVM_ARCHES = ("x64", "ARM64")
FALLBACK_GLOBS = (
    "Microsoft Visual Studio/*/*/VC/Tools/Llvm/x64/bin/clang-format.exe",
    "Microsoft Visual Studio/*/*/VC/Tools/Llvm/ARM64/bin/clang-format.exe",
    "LLVM/bin/clang-format.exe",
)
LOCAL_APP_DATA_FALLBACK_GLOBS = ("Programs/LLVM/bin/clang-format.exe",)

# Budgets of a child process. The probe calls are short by nature, while one
# clang-format run over one file gets the budget of the run.
PROBE_TIMEOUT_SECONDS = 60
DEFAULT_TIMEOUT_SECONDS = 120
DEFAULT_JOBS_CAP = 8

# Result of one file.
STATUS_CHANGED = "changed"
STATUS_UNCHANGED = "unchanged"
STATUS_NOT_FORMATTED = "not formatted"
STATUS_FAILED = "failed"

# Marker clang-format puts into its diagnostics when --Werror reports a file
# which is not formatted.
VIOLATION_MARKER = "clang-format-violations"

EXIT_OK = 0
EXIT_FAILURE = 1
EXIT_USAGE = 2


class ClangFormatNotFoundError(RuntimeError):
    """Raised when no usable clang-format executable could be located."""


@dataclass(frozen=True)
class FileOutcome:
    """Result of the clang-format run of a single source file."""

    path: Path
    status: str
    detail: str = ""


@dataclass
class BatchResult:
    """Result of a run over every source file of the repository."""

    outcomes: list[FileOutcome] = field(default_factory=list)
    elapsed_seconds: float = 0.0

    @property
    def scanned(self) -> int:
        """Number of files the run looked at."""
        return len(self.outcomes)

    def count(self, status: str) -> int:
        """Number of files which ended with ``status``."""
        return sum(1 for outcome in self.outcomes if outcome.status == status)

    @property
    def failures(self) -> list[FileOutcome]:
        """Files clang-format could not process."""
        return [outcome for outcome in self.outcomes if outcome.status == STATUS_FAILED]

    @property
    def not_formatted(self) -> list[FileOutcome]:
        """Files which are not formatted, as reported by the check mode."""
        return [outcome for outcome in self.outcomes if outcome.status == STATUS_NOT_FORMATTED]


def resolve_repo_root() -> Path:
    """Return the root of the repository which holds this script."""
    return Path(__file__).resolve().parents[1]


def default_jobs() -> int:
    """Return the default degree of parallelism of a run."""
    return max(1, min(os.cpu_count() or 1, DEFAULT_JOBS_CAP))


def _run(command: list[str], timeout: int, cwd: Path | None = None) -> subprocess.CompletedProcess[bytes]:
    """Run a command without a shell and capture its output as bytes.

    The output is captured as bytes on purpose: a text mode pipe would
    translate the line endings on Windows, and the check mode compares the
    output of clang-format with the bytes of the file on disk.
    """
    return subprocess.run(
        command,
        cwd=None if cwd is None else str(cwd),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        timeout=timeout,
        check=False,
    )


def _decode(data: bytes) -> str:
    """Decode the output of a child process for the log."""
    return data.decode("utf-8", errors="replace").strip()


def _failure_detail(result: subprocess.CompletedProcess[bytes]) -> str:
    """Return the first line of the diagnostics of a failed child process."""
    text = _decode(result.stderr) or _decode(result.stdout)
    if not text:
        return f"clang-format exited with {result.returncode} and wrote no message"
    return text.splitlines()[0]


def _unique(paths: Iterable[Path]) -> list[Path]:
    """Return the paths without a repetition, keeping the order."""
    seen: set[str] = set()
    unique: list[Path] = []
    for path in paths:
        key = str(path).lower()
        if key not in seen:
            seen.add(key)
            unique.append(path)
    return unique


def _environment_directory(variable: str, default: str | None = None) -> Path | None:
    """Return the directory a well known environment variable names."""
    value = os.environ.get(variable)
    if value:
        return Path(value)
    return Path(default) if default else None


def _probe(candidate: Path) -> str | None:
    """Return the version clang-format reports, or None when it is unusable."""
    if not candidate.is_file():
        return None
    try:
        result = _run([str(candidate), "--version"], PROBE_TIMEOUT_SECONDS)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if result.returncode != 0:
        return None
    return _decode(result.stdout) or _decode(result.stderr) or "unknown version"


def _vswhere_executable() -> Path | None:
    """Return the path of vswhere.exe, or None when it is not installed."""
    candidates = (
        ("ProgramFiles(x86)", r"C:\Program Files (x86)"),
        ("ProgramFiles", r"C:\Program Files"),
    )
    for variable, default in candidates:
        directory = _environment_directory(variable, default)
        if directory is None:
            continue
        vswhere = directory / VSWHERE_RELATIVE_PATH
        if vswhere.is_file():
            return vswhere
    return None


def _visual_studio_installations() -> tuple[list[Path], str]:
    """Return the Visual Studio installation directories and a log note.

    Every installation is reported, not only the most recent one, so that a
    machine which carries a Community and an Enterprise installation of
    different versions finds the first one which ships clang-format.
    """
    vswhere = _vswhere_executable()
    if vswhere is None:
        return [], "vswhere.exe was not found, the Visual Studio installations were not enumerated"
    try:
        result = _run(
            [str(vswhere), "-products", "*", "-prerelease", "-property", "installationPath"],
            PROBE_TIMEOUT_SECONDS,
        )
    except (OSError, subprocess.TimeoutExpired) as error:
        return [], f"vswhere.exe could not be run: {error}"
    if result.returncode != 0:
        return [], f"vswhere.exe exited with {result.returncode}: {_failure_detail(result)}"
    installations = [Path(line.strip()) for line in _decode(result.stdout).splitlines() if line.strip()]
    if not installations:
        return [], "vswhere.exe reported no Visual Studio installation"
    return installations, ""


def _visual_studio_candidates(installation: Path) -> list[Path]:
    """Return the clang-format candidates of one Visual Studio installation."""
    llvm = installation / VS_LLVM_RELATIVE_PATH
    candidates = [llvm / arch / "bin" / "clang-format.exe" for arch in VS_LLVM_ARCHES]
    candidates.extend(sorted(llvm.glob("*/bin/clang-format.exe")))
    return _unique(candidates)


def _fallback_candidates() -> list[Path]:
    """Return the well known clang-format locations outside of the PATH."""
    bases: list[tuple[Path, tuple[str, ...]]] = []
    for variable, default in (("ProgramFiles", r"C:\Program Files"), ("ProgramFiles(x86)", r"C:\Program Files (x86)")):
        directory = _environment_directory(variable, default)
        if directory is not None:
            bases.append((directory, FALLBACK_GLOBS))
    local_app_data = _environment_directory("LOCALAPPDATA")
    if local_app_data is not None:
        bases.append((local_app_data, LOCAL_APP_DATA_FALLBACK_GLOBS))

    candidates: list[Path] = []
    for base, patterns in bases:
        if not base.is_dir():
            continue
        for pattern in patterns:
            candidates.extend(sorted(base.glob(pattern)))
    return _unique(candidates)


def find_clang_format() -> tuple[Path, str, str]:
    """Locate a usable clang-format and report where it came from.

    Returns the executable, the origin of the candidate and the version it
    reported. Raises ClangFormatNotFoundError when no candidate is usable.
    """
    attempts: list[str] = []

    on_path = shutil.which("clang-format")
    if on_path is None:
        attempts.append("the PATH does not hold clang-format")
    else:
        version = _probe(Path(on_path))
        if version is not None:
            return Path(on_path), "PATH", version
        attempts.append(f"the clang-format of the PATH is not usable: {on_path}")

    installations, note = _visual_studio_installations()
    if note:
        attempts.append(note)
    for installation in installations:
        for candidate in _visual_studio_candidates(installation):
            version = _probe(candidate)
            if version is not None:
                return candidate, "Visual Studio", version
            attempts.append(f"not usable: {candidate}")

    for candidate in _fallback_candidates():
        version = _probe(candidate)
        if version is not None:
            return candidate, "well known path", version
        attempts.append(f"not usable: {candidate}")

    raise ClangFormatNotFoundError("\n".join(f"  - {attempt}" for attempt in attempts))


def _is_excluded_directory(name: str) -> bool:
    """Tell whether a directory is skipped while the repository is walked."""
    lowered = name.lower()
    if lowered in EXCLUDED_DIR_NAMES:
        return True
    return lowered.startswith(EXCLUDED_DIR_PREFIXES)


def iter_source_files(root: Path) -> list[Path]:
    """Return every source file of the repository below ``root``.

    The generated and the tool owned directories are pruned while the tree is
    walked, and the result is sorted so that two runs report the same order.
    """
    files: list[Path] = []
    for current, directories, names in os.walk(root):
        directories[:] = [name for name in directories if not _is_excluded_directory(name)]
        for name in names:
            if Path(name).suffix.lower() in SOURCE_SUFFIXES:
                files.append(Path(current) / name)
    files.sort(key=lambda path: str(path).lower())
    return files


def _command_line(clang_format: Path, relative: Path, extra: tuple[str, ...]) -> list[str]:
    """Build the command line of one clang-format run over one file.

    ``--style=file`` selects the ``.clang-format`` of the repository, which
    clang-format looks up from the directory of the file upwards.
    ``--fallback-style=none`` keeps the built in LLVM style out of the way: a
    file whose style file cannot be reached is handed back unchanged instead of
    being rewritten with the wrong style. A repository without a style file is
    refused before the first file is touched.
    """
    return [str(clang_format), *extra, "--style=file", "--fallback-style=none", str(relative)]


def _format_in_place(clang_format: Path, root: Path, relative: Path, timeout: int) -> FileOutcome:
    """Format one file in place and tell whether clang-format changed it."""
    target = root / relative
    try:
        before = target.read_bytes()
    except OSError as error:
        return FileOutcome(target, STATUS_FAILED, f"the file could not be read: {error}")
    try:
        result = _run(_command_line(clang_format, relative, ("-i",)), timeout, cwd=root)
    except subprocess.TimeoutExpired:
        return FileOutcome(target, STATUS_FAILED, f"clang-format did not finish within {timeout} seconds")
    except OSError as error:
        return FileOutcome(target, STATUS_FAILED, f"clang-format could not be run: {error}")
    if result.returncode != 0:
        return FileOutcome(target, STATUS_FAILED, _failure_detail(result))
    try:
        after = target.read_bytes()
    except OSError as error:
        return FileOutcome(target, STATUS_FAILED, f"the file could not be read back: {error}")
    return FileOutcome(target, STATUS_UNCHANGED if after == before else STATUS_CHANGED)


def _check_with_werror(clang_format: Path, root: Path, relative: Path, timeout: int) -> FileOutcome:
    """Check one file with ``--dry-run --Werror``, which writes nothing."""
    target = root / relative
    try:
        result = _run(_command_line(clang_format, relative, ("--dry-run", "--Werror")), timeout, cwd=root)
    except subprocess.TimeoutExpired:
        return FileOutcome(target, STATUS_FAILED, f"clang-format did not finish within {timeout} seconds")
    except OSError as error:
        return FileOutcome(target, STATUS_FAILED, f"clang-format could not be run: {error}")
    if result.returncode == 0:
        return FileOutcome(target, STATUS_UNCHANGED)
    if VIOLATION_MARKER in _decode(result.stderr):
        return FileOutcome(target, STATUS_NOT_FORMATTED, "the file is not clang-formatted")
    return FileOutcome(target, STATUS_FAILED, _failure_detail(result))


def _check_with_output(clang_format: Path, root: Path, relative: Path, timeout: int) -> FileOutcome:
    """Check one file by comparing the formatted text with the file.

    This is the fallback for a clang-format which does not know ``--Werror``.
    It writes nothing either.
    """
    target = root / relative
    try:
        result = _run(_command_line(clang_format, relative, ()), timeout, cwd=root)
    except subprocess.TimeoutExpired:
        return FileOutcome(target, STATUS_FAILED, f"clang-format did not finish within {timeout} seconds")
    except OSError as error:
        return FileOutcome(target, STATUS_FAILED, f"clang-format could not be run: {error}")
    if result.returncode != 0:
        return FileOutcome(target, STATUS_FAILED, _failure_detail(result))
    try:
        current = target.read_bytes()
    except OSError as error:
        return FileOutcome(target, STATUS_FAILED, f"the file could not be read: {error}")
    if current == result.stdout:
        return FileOutcome(target, STATUS_UNCHANGED)
    return FileOutcome(target, STATUS_NOT_FORMATTED, "the file differs from the formatted output")


def _supports_werror(clang_format: Path) -> bool:
    """Tell whether clang-format knows the --Werror switch of the check mode."""
    try:
        result = _run([str(clang_format), "--help"], PROBE_TIMEOUT_SECONDS)
    except (OSError, subprocess.TimeoutExpired):
        return False
    return b"--Werror" in result.stdout + result.stderr


def run_batch(
    root: Path,
    files: list[Path],
    clang_format: Path,
    *,
    check: bool,
    jobs: int,
    timeout: int,
) -> BatchResult:
    """Run clang-format over every file and collect one outcome per file.

    A file which fails or which runs into the timeout does not stop the run:
    its outcome carries the diagnostics and the remaining files are handled.
    """
    if check:
        runner = _check_with_werror if _supports_werror(clang_format) else _check_with_output
    else:
        runner = _format_in_place

    def run_one(path: Path) -> FileOutcome:
        return runner(clang_format, root, path, timeout)

    started = time.monotonic()
    if jobs <= 1:
        outcomes = [run_one(path) for path in files]
    else:
        # subprocess releases the interpreter lock while it waits, so the
        # calls overlap. The map keeps the order of the input.
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            outcomes = list(pool.map(run_one, files))
    return BatchResult(outcomes=outcomes, elapsed_seconds=time.monotonic() - started)


def report(result: BatchResult, root: Path, *, check: bool, verbose: bool) -> None:
    """Print the outcome of a run, one line per file which needs attention."""
    for outcome in result.outcomes:
        relative = outcome.path.relative_to(root)
        if outcome.status == STATUS_FAILED:
            print(f"FAILED        {relative}: {outcome.detail}")
        elif outcome.status == STATUS_NOT_FORMATTED:
            print(f"NOT FORMATTED {relative}")
        elif verbose:
            print(f"{outcome.status.upper():<13} {relative}")

    failed = len(result.failures)
    if check:
        not_formatted = len(result.not_formatted)
        print(
            f"Summary: {result.scanned} scanned, {not_formatted} not formatted, "
            f"{result.count(STATUS_UNCHANGED)} formatted, {failed} failed, "
            f"{result.elapsed_seconds:.1f}s"
        )
    else:
        print(
            f"Summary: {result.scanned} scanned, {result.count(STATUS_CHANGED)} changed, "
            f"{result.count(STATUS_UNCHANGED)} unchanged, {failed} failed, "
            f"{result.elapsed_seconds:.1f}s"
        )


def build_argument_parser() -> argparse.ArgumentParser:
    """Return the command line parser of the script."""
    parser = argparse.ArgumentParser(
        prog="format.py",
        description="Format every .hpp and .cpp file of the appbox repository with clang-format.",
    )
    parser.add_argument(
        "--check",
        "--dry-run",
        action="store_true",
        dest="check",
        help="report the files which are not formatted instead of writing them",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=0,
        metavar="N",
        help=f"number of parallel clang-format runs; default the number of processors, at most {DEFAULT_JOBS_CAP}",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT_SECONDS,
        metavar="SECONDS",
        help=f"budget of one clang-format run, in seconds; default {DEFAULT_TIMEOUT_SECONDS}",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="name the result of every file and not only of the files which need attention",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    """Run the script and return its exit code."""
    parser = build_argument_parser()
    arguments = parser.parse_args(argv)
    if arguments.jobs < 0:
        parser.error("--jobs must not be negative")
    if arguments.timeout <= 0:
        parser.error("--timeout must be a positive number of seconds")

    root = resolve_repo_root()
    style_file = root / STYLE_FILE_NAME
    if not style_file.is_file():
        print(f"error: {style_file} does not exist, so the repository root is not a checkout", file=sys.stderr)
        return EXIT_USAGE

    try:
        clang_format, origin, version = find_clang_format()
    except ClangFormatNotFoundError as error:
        print("error: no usable clang-format was found; the candidates below were tried:", file=sys.stderr)
        print(str(error), file=sys.stderr)
        return EXIT_USAGE

    files = iter_source_files(root)
    if not files:
        print(f"error: no .hpp or .cpp file was found below {root}", file=sys.stderr)
        return EXIT_USAGE

    jobs = arguments.jobs if arguments.jobs > 0 else default_jobs()
    print(f"clang-format: {clang_format} ({origin})")
    print(f"version:      {version}")
    print(f"repository:   {root}")
    print(f"mode:         {'check only' if arguments.check else 'format in place'}")
    print(f"files:        {len(files)}")
    sys.stdout.flush()

    result = run_batch(root, files, clang_format, check=arguments.check, jobs=jobs, timeout=arguments.timeout)
    report(result, root, check=arguments.check, verbose=arguments.verbose)

    if result.failures or result.not_formatted:
        return EXIT_FAILURE
    return EXIT_OK


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        raise SystemExit(130)

#!/usr/bin/env python3
"""Bundle the Fedora MinGW runtime beside the executable, without host DLLs."""

# These modules are part of Python's standard library (no pip install needed).
# argparse reads command-line arguments; Path represents files and directories.
# shutil copies files and folders; subprocess runs an external command.
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile


def _copy_license_file(source, destination):
    """Copy a notice, repairing the known broken Fedora MinGW GLib link."""
    source = Path(source)
    # Fedora 40's mingw64-glib2/COPYING points to a missing LICENSES folder.
    # Native GLib installs the same LGPL text under its full license name.
    # Use that text only for this specific broken link; other errors still fail.
    if (source.parent.name == "mingw64-glib2" and source.name == "COPYING"
            and source.is_symlink() and not source.exists()):
        source = Path("/usr/share/licenses/glib2/LGPL-2.1-or-later.txt")
    return shutil.copy2(source, destination)


def package(executable, prefix, output):
    """Create a Windows distribution folder from a compiled executable.

    executable: Path to the Windows .exe that has already been built.
    prefix: Path to the MinGW installation containing Windows dependencies.
    output: Path to a new folder where the complete package will be placed.

    All three arguments are Path objects, not plain strings.
    """
    if output.exists():
        raise SystemExit(f"Destination already exists: {output}; choose a fresh directory")

    # Build in a temporary directory beside the final destination.
    # The 'with' block automatically removes this temporary directory on exit,
    # including when copying fails. Existing user folders are never deleted.
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".windows-package-", dir=output.parent) as temporary:
        staged = Path(temporary) / output.name
        count = _assemble_package(executable, prefix, staged)

        # Publish the folder only after every packaging step has succeeded.
        # Both paths are on the same filesystem, so rename does not recopy it.
        staged.rename(output)

    print(f"Packaged {executable.name} with {count} DLLs in {output}")


def _assemble_package(executable, prefix, output):
    """Fill a fresh staging folder and return the number of copied DLLs.

    The leading underscore marks this as a helper for this module.
    package() handles the temporary folder and the final destination.
    """
    # 1. Check the inputs before creating or copying anything.
    # Refuse an existing destination so old and new DLLs cannot get mixed.
    # Raising SystemExit stops the script and displays the supplied error.
    if output.exists():
        raise SystemExit(f"Destination already exists: {output}; choose a fresh directory")

    # With Path objects, / joins path components; it does not divide numbers.
    # glob("*.dll") finds DLL files, and sorted() puts them in a stable order.
    dlls = sorted((prefix / "bin").glob("*.dll"))
    if not dlls:
        raise SystemExit(f"No Windows DLLs found in {prefix / 'bin'}")

    # Each string is the beginning of a required library's file name.
    # any(...) is True when at least one DLL matches that beginning.
    # lower() makes this comparison independent of uppercase/lowercase letters.
    for library in ("libgtk-4-", "libpoppler-glib-", "libarchive-", "liblzma-"):
        if not any(dll.name.lower().startswith(library) for dll in dlls):
            raise SystemExit(f"Required runtime {library}*.dll missing in {prefix / 'bin'}")
    if not executable.is_file():
        raise SystemExit(f"Executable not found: {executable}")

    # 2. Create the destination and copy the executable into it.
    # parents=True also creates missing parent directories.
    # .name returns only the file name, without its parent directories.
    # copy2() copies a file and attempts to preserve metadata, such as its date.
    output.mkdir(parents=True)
    shutil.copy2(executable, output / executable.name)

    # 3. Place the Windows DLLs beside the executable.
    # Include dynamically loaded libraries as well as PE imports. All come from
    # the same toolchain, including Poppler, GTK, libstdc++, libgcc and winpthread.
    # PE imports are dependencies listed inside a Windows executable or DLL.
    for dll in dlls:
        shutil.copy2(dll, output / dll.name)

    # 4. Copy runtime data, configuration and optional plugin directories.
    # A relative path is interpreted inside prefix or output, respectively.
    # copytree() copies an entire directory, including its subdirectories.
    # dirs_exist_ok=True allows these copies to share parent directories.
    # Copy only application configuration from etc. The full etc tree also
    # contains certificate infrastructure (etc/pki/CA) with restricted access;
    # it is not application configuration and must not be shipped wholesale.
    for relative in ("share", "etc/fonts", "etc/gtk-4.0",
                     "lib/gio", "lib/gdk-pixbuf-2.0"):
        source = prefix / relative
        if source.exists():
            shutil.copytree(source, output / relative, dirs_exist_ok=True)
    # 5. Compile GLib settings definitions into the cache used at runtime.
    # subprocess.run() waits for the external tool to finish.
    # check=True raises an error if the tool returns a failure exit code.
    # str() converts the Path object to text for the command argument.
    schemas = output / "share/glib-2.0/schemas"
    if schemas.is_dir():
        subprocess.run(["glib-compile-schemas", str(schemas)], check=True)
    # 6. Include dependency license notices and StudyStudio's own license.
    # Fedora RPM packages keep these notices outside the MinGW directory.
    for source in sorted(Path("/usr/share/licenses").glob("mingw64-*")):
        if source.is_dir():
            # copy_function selects how each individual file is copied.
            shutil.copytree(source, output / "licenses" / source.name,
                            copy_function=_copy_license_file)
    shutil.copy2("LICENSE", output / "LICENSE-StudyStudio")

    # 7. Write short instructions for the person receiving the Windows folder.
    # Adjacent string literals form one string; \n starts a new line.
    # UTF-8 specifies how text is encoded when written to the file.
    (output / "README.txt").write_text(
        "Keep this entire folder together; run the .exe directly.\n"
        "LibreOffice is not bundled: install 64-bit LibreOffice separately.\n"
        "STUDYSTUDIO_LO_PATH can point to its program directory.\n"
        "See licenses/ for dependency notices.\n", encoding="utf-8")
    # Return the count to package(), which prints success after publication.
    return len(dlls)


if __name__ == "__main__":
    # This block runs when Python executes this file directly, but not when
    # another Python file imports it to reuse the package() function.
    # Example (run from the repository root, inside the Docker builder):
    # python3 scripts/package-windows.py build/app.exe build/windows-package
    # The repository root must be the working directory so LICENSE can be found.

    # Build the command-line parser. __doc__ is the module description above;
    # argparse also provides a --help option automatically.
    parser = argparse.ArgumentParser(description=__doc__)

    # These two arguments are required, in this order.
    # type=Path converts their text values into Path objects.
    parser.add_argument("executable", type=Path)
    parser.add_argument("output", type=Path)

    # --prefix is optional; without it, use Fedora's MinGW installation path.
    parser.add_argument("--prefix", type=Path,
                        default=Path("/usr/x86_64-w64-mingw32/sys-root/mingw"))
    # Parse the supplied arguments, then pass their values to our function.
    args = parser.parse_args()
    package(args.executable, args.prefix, args.output)

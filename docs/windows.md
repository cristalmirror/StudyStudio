# Windows packaging and runtime checks

From the repository root on Linux, build the toolchain and package Windows:

```bash
docker build -t studystudio-builder .
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD":/usr/src/app \
  -w /usr/src/app \
  studystudio-builder make win64-package
```

Run this target inside Docker: the MinGW DLL directory belongs to the Fedora
image, not the Debian host. `make all` builds standalone Linux and Windows
binaries; `make win64-package` additionally gathers the Windows runtime.

Rebuild the Docker image after dependency changes, then run `make win64-package`
inside it. This creates `build/studystudio-0.1.3_windows/`. Copy the entire
directory to Windows; the DLLs must remain beside the executable and the data
directories must retain their structure. Repackaging refuses an existing output
directory to avoid mixing old and new runtimes. Choose another destination with
`WIN64_PACKAGE_DIR=build/windows-new`.

Packaging uses a temporary directory and publishes the destination only after
all steps succeed. Only `etc/fonts` and `etc/gtk-4.0` configuration is copied;
the builder's certificate infrastructure under `etc/pki` is excluded. If an
older script left a partial destination, choose a fresh output directory.
Mount the entire repository into the container to use the current script,
rather than the copy embedded when the image was built.

Fedora 40's `mingw64-glib2/COPYING` license symlink is broken. The packager
copies the same LGPL text from native GLib's installed license directory for
that specific link; other copying errors still fail the operation.

To create a ZIP on Linux after packaging:

```bash
python3 -m zipfile -c build/studystudio-0.1.3_windows.zip build/studystudio-0.1.3_windows
python3 -m zipfile -t build/studystudio-0.1.3_windows.zip
```

Extract the entire ZIP on Windows before launching the executable.

The packaging process was tested in the rebuilt Fedora image: it gathered
137 DLLs, and the resulting ZIP passed an archive integrity check. A simulated
copy failure also confirmed that the temporary directory is removed and no
partial destination is published. These changes remain part of version 0.1.3.
Rebuild and repackage after source changes to refresh existing artifacts. Windows
application runtime checks below remain pending, and CI does not create a ZIP.

The package contains the MinGW runtime from the builder (GTK, Poppler,
libarchive, liblzma and their dependencies), GTK data and dependency notices.
LibreOffice remains a separate 64-bit installation, normally at
`C:\Program Files\LibreOffice\program`. For another installation set
`STUDYSTUDIO_LO_PATH` to its `program` directory. The loader converts UTF-8 paths
to UTF-16 and searches for dependencies beside the loaded LibreOffice DLL.

Run these checks on Windows without MinGW/MSYS2 on `PATH`:

1. Start the packaged executable directly from Explorer. Confirm that there is
   no missing-DLL dialog. Repeat from a directory containing spaces and accents.
2. Create a subject named `Física` and check its folder and all subfolders.
   Creating it again must report that it exists.
3. Save a Windows subject containing `notas\semana1\resúmen.txt` with known
   contents. Load the saved `good.tar.xz` and compare extracted file bytes. The
   extraction directory is `good.tar`, because only the final suffix is removed.
   Also test truncated input and confirm an error rather than a freeze.
4. Open an office document with LibreOffice in its default installation path;
   repeat with `STUDYSTUDIO_LO_PATH` set to a path containing spaces and accents.
   Close StudyStudio and check that no crash dialog appears.
5. Open a PDF with the packaged Poppler DLLs, add an annotation, save, close and
   reopen it. Confirm that the annotation persists.

Windows subject archives currently contain custom records compressed with XZ,
even if named `.tar.xz`; they are not Linux TAR archives. Test a file saved by
the Windows build for step 3. This change does not add cross-platform archive
compatibility or mark any of these runtime checks as passed.

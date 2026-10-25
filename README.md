# StudyStudio

> A cross-platform GTK4 desktop application written in C, built with a unified Docker-based toolchain for Linux and Windows.

[![Version](https://img.shields.io/badge/version-0.1.3-orange)](./CHANGELOG.md)
[![License](https://img.shields.io/badge/license-GPLv3-green)](./LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey)]()
[![GTK](https://img.shields.io/badge/GTK-4.0-8A2BE2)](https://www.gtk.org/)

---

> [!WARNING]
> **This project is in early development (v0.1.3).** The build system is fully functional, but the application itself is not yet complete. Use for development and learning purposes only.

---

## 📖 About

StudyStudio is a desktop application built with **C** and **GTK4**, designed to demonstrate a clean, modern workflow for developing cross-platform GUI applications. It uses a **Docker-based build environment** (Fedora 40) to compile for both Linux and Windows from a single command, without polluting the host system.

The project showcases:
- Separation of UI (XML) from logic (C)
- Cross-compilation via MinGW-w64
- Dynamic widget creation at runtime
- Modern GTK4 best practices

---

## ✨ Features

- 🖥️ **Cross-platform**: Single codebase compiles for both Linux and Windows
- 🐳 **Docker-based build**: No need to install GTK4 or MinGW on your host
- 🎨 **XML-based UI**: Clean separation between interface and logic using `GtkBuilder`
- 📦 **Dynamic widgets**: Add elements to the UI at runtime
- 📜 **Scrollable content**: `GtkScrolledWindow` for handling long lists
- ⚡ **Fast builds**: Uses precompiled Fedora packages instead of building from source
- 📄 **Office documents (in development)**: `.docx`, `.odt`, `.xlsx`, `.pptx`... are opened through LibreOfficeKit; it needs LibreOffice installed at run time (or `STUDYSTUDIO_LO_PATH` pointing to its `program` folder)
- 📕 **PDF (in development)**: `.pdf` files are shown with Poppler (poppler-glib) and can be annotated (highlight, rectangle, sticky notes, erase) and saved

---

## Compilation

The current source version is **0.1.3**, defined by `VERSION` in the Makefile
and displayed in the XML window title. The Windows runtime package includes
GTK and Poppler DLLs; LibreOffice remains a separate installation.

Run from the repository root on Linux. Docker provides GTK, Poppler and the
MinGW Windows dependencies; plain `make` on a host without them will fail.

```bash
docker build -t studystudio-builder .
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD":/usr/src/app \
  -w /usr/src/app \
  studystudio-builder make all

# Run the Linux binary on Linux.
./build/studystudio-0.1.3_linux
```

To distribute Windows, additionally assemble the executable and its DLLs:

```bash
docker run --rm \
  --user "$(id -u):$(id -g)" \
  -v "$PWD":/usr/src/app \
  -w /usr/src/app \
  studystudio-builder make win64-package
```

Copy the entire `build/studystudio-0.1.3_windows/` folder to Windows and run
its `.exe`. `make all` alone does not gather DLLs. Install 64-bit LibreOffice
separately for office documents. Packaging requires a fresh destination;
set `WIN64_PACKAGE_DIR=build/windows-new` to use another folder.
See [Windows packaging and runtime checks](docs/windows.md) for ZIP commands,
troubleshooting and the remaining Windows checks. Rebuild and repackage after source changes to refresh existing binaries and ZIPs.

## 🐞 Debugging with GDB

The Makefile has a `debug` target that compiles an unoptimized build with debug symbols (`-g -O0`), placed in `build/linux-debug/` so it never mixes with the release object files in `build/linux/`.

Build it the same way as the release binaries, inside the Docker toolchain (the host lacks `gtk4-devel`/`libarchive-devel`, so plain `make` only works in the container):
### GNU/Linux 
```
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$(pwd)/src:/usr/src/app/src" \
    -v "$(pwd)/include:/usr/src/app/include" \
    -v "$(pwd)/build:/usr/src/app/build" \
    -v "$(pwd)/Makefile:/usr/src/app/Makefile:ro" \
    -v "$(pwd)/resources.xml:/usr/src/app/resources.xml:ro" \
    -v "$(pwd)/interface.ui:/usr/src/app/interface.ui:ro" \
    studystudio-builder \
    make debug
```
### Windows

```
docker run --rm \
            --user "$(id -u):$(id -g)" \
            -v "$(pwd)/src:/usr/src/app/src" \
            -v "$(pwd)/include:/usr/src/app/include" \
            -v "$(pwd)/build:/usr/src/app/build" \
            -v "$(pwd)/Makefile:/usr/src/app/Makefile:ro" \
            -v "$(pwd)/resources.xml:/usr/src/app/resources.xml:ro" \
            -v "$(pwd)/interface.ui:/usr/src/app/interface.ui:ro" \
            studystudio-builder \
            make win64-debug
```
Once the debug binary exists, run GDB directly on the host, the same way you already run the release binary:

```
gdb ./build/studystudio-0.1.3_linux_debug
```

Inside GDB:
```
(gdb) break main
(gdb) run
(gdb) bt        # backtrace after a crash
```

There's also a `make gdb` shortcut that rebuilds `debug` and launches GDB in one step — use it on the host after the object files exist, since the container isn't set up to run GTK windows or attach a debugger interactively:

```
make gdb
```

A Windows debug variant is also available via `make win64-debug`, producing `build/studystudio-0.1.3_win64_debug.exe` with the same `-g -O0 -DDEBUG` flags, in its own `build/win64-debug/` object directory. There is no `gdb`-equivalent shortcut for it: cross-debugging a MinGW-w64 binary needs `winedbg`/Wine, which this toolchain does not set up, so the binary is for standalone testing (e.g. running it under Wine) rather than in-container debugging.

## 📂 Project Structure

```text
StudyStudio/
├── .github/workflows/
│   └── build.yml              # Linux/Windows builds and Windows package artifacts
├── src/
│   ├── main.c                 # Application entry point and UI callbacks
│   ├── subject.c              # Subject folders, archive save/load and extraction
│   ├── edit.c                 # Edit base class, editor kinds and factory
│   ├── menu.c                 # Drop-down menu base class
│   ├── edit_subclass/
│   │   ├── editTXT.c          # Plain-text editor (.txt, .md)
│   │   ├── editDOC.c          # Office viewer through LibreOfficeKit
│   │   └── editPDF.c          # PDF viewer and annotations through Poppler
│   └── msc/
│       ├── menuEditKind.c     # Subject document-kind menu
│       └── subjectDialog.c    # New subject dialog
├── include/
│   ├── subject.h              # Subject data and public operations
│   ├── edit.h                 # Edit base class interface
│   ├── menu.h                 # Menu base class interface
│   ├── edit_subclass/         # Editor headers, including the editUML.h placeholder
│   ├── msc/                   # Subject menu and dialog headers
│   └── third_party/LibreOfficeKit/ # Vendored LOK headers, patched for C/Windows
├── scripts/
│   └── package-windows.py     # Assemble the executable, DLLs, GTK data and notices
├── docs/
│   ├── README.md              # Documentation index and development guidelines
│   ├── windows.md             # Windows packaging, ZIP commands and runtime checks
│   ├── class_diagram.md       # Application class diagram
│   ├── prototype_ia.md        # Development session history
│   └── *.md                   # Source, interface and build documentation
├── build/                     # Generated outputs (ignored by Git)
│   ├── linux/                 # Linux release object files
│   ├── linux-debug/           # Linux debug object files
│   ├── win64/                 # Windows release object files
│   ├── win64-debug/           # Windows debug object files
│   ├── resources.c            # Generated embedded GTK interface
│   ├── studystudio-0.1.3_linux
│   ├── studystudio-0.1.3_linux_debug
│   ├── studystudio-0.1.3_win64.exe
│   ├── studystudio-0.1.3_win64_debug.exe
│   └── studystudio-0.1.3_windows/ # Windows distribution with DLLs and runtime data
├── interface.ui               # GTK4 interface definition (XML)
├── resources.xml              # Embedded resource manifest
├── Makefile                   # Release/debug builds and Windows packaging
├── Dockerfile                 # Fedora 40 Linux/MinGW build environment
├── CODEX.md                   # Codex collaboration and code-comment conventions
├── CLAUDE.md                  # Claude collaboration context
├── .gitignore                 # Excluded build outputs and local files
├── LICENSE                    # GPLv3 license
├── CHANGELOG.md               # Version history
└── README.md                  # Project overview and build instructions
```

Files under `build/` appear when their corresponding targets run. Windows
package directories can have another name when `WIN64_PACKAGE_DIR` is set.
ZIP archives are created separately using the commands in [docs/windows.md](docs/windows.md).

## 📝 Commit Conventions

This project follows [Conventional Commits](https://www.conventionalcommits.org/). Prefix each commit message with one of the following types:

| Type | Meaning |
| --- | --- |
| `feat` | A new feature. |
| `fix` | A bug fix. |
| `docs` | Documentation-only changes (e.g. `README.md`). |
| `refactor` | Code changes that neither fix a bug nor add a feature (performance improvements, restructuring). |
| `test` | Adding or modifying unit or integration tests. |
| `chore` | Maintenance tasks, dependency updates, or build/CI configuration. |

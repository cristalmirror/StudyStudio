# StudyStudio

> A cross-platform GTK4 desktop application written in C, built with a unified Docker-based toolchain for Linux and Windows.

[![Version](https://img.shields.io/badge/version-0.0.11-orange)](./CHANGELOG.md)
[![License](https://img.shields.io/badge/license-GPLv3-green)](./LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey)]()
[![GTK](https://img.shields.io/badge/GTK-4.0-8A2BE2)](https://www.gtk.org/)

---

> [!WARNING]
> **This project is in early development (v0.0.5).** The build system is fully functional, but the application itself is not yet complete. Use for development and learning purposes only.

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

---

## Compilation 

```
cd ~/StudyStudio

docker build -t mi_app_builder .

mkdir -p build

docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$(pwd)/src:/usr/src/app/src" \
    -v "$(pwd)/include:/usr/src/app/include" \
    -v "$(pwd)/build:/usr/src/app/build" \
    -v "$(pwd)/Makefile:/usr/src/app/Makefile:ro" \
    -v "$(pwd)/resources.xml:/usr/src/app/resources.xml:ro" \
    -v "$(pwd)/interface.ui:/usr/src/app/interface.ui:ro" \
    mi_app_builder \
    make all
      
# to run LINUX
./build/studystudio-0.0.x_linux

#to run WINDOWS
./build/studystudio-0.0.x_win64.exe

```

## 🐞 Debugging with GDB

The Makefile has a `debug` target that compiles an unoptimized build with debug symbols (`-g -O0`), placed in `build/linux-debug/` so it never mixes with the release object files in `build/linux/`.

Build it the same way as the release binaries, inside the Docker toolchain (the host lacks `gtk4-devel`/`libarchive-devel`, so plain `make` only works in the container):

```
docker run --rm \
    --user "$(id -u):$(id -g)" \
    -v "$(pwd)/src:/usr/src/app/src" \
    -v "$(pwd)/include:/usr/src/app/include" \
    -v "$(pwd)/build:/usr/src/app/build" \
    -v "$(pwd)/Makefile:/usr/src/app/Makefile:ro" \
    -v "$(pwd)/resources.xml:/usr/src/app/resources.xml:ro" \
    -v "$(pwd)/interface.ui:/usr/src/app/interface.ui:ro" \
    mi_app_builder \
    make debug
```

Once the debug binary exists, run GDB directly on the host, the same way you already run the release binary:

```
gdb ./build/studystudio-0.0.x_linux_debug
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

## 📂 Project Structure
```
StudyStudio/
├── src/
│ ├── main.c # Application entry point and callbacks
│ └── subject.c # Implementations 
├── build/
│ ├── linux/ # Linux object files (release)
│ ├── linux-debug/ # Linux object files (debug, for GDB)
│ └── win64/ # Windows object files
├──include
│ └ subject.h #definitions subject manager
├── interface.ui # GTK4 UI definition (XML)
├── resources.xml
├── docker build -t mi_app_builder .
├── Makefile # Build rules for both platforms
├── Dockerfile # Fedora 40 build environment
├── CHANGELOG.md # Version history
└── README.md # This file

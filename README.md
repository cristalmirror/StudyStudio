# StudyStudio

> A cross-platform GTK4 desktop application written in C, built with a unified Docker-based toolchain for Linux and Windows.

[![Version](https://img.shields.io/badge/version-0.0.1-orange)](./CHANGELOG.md)
[![License](https://img.shields.io/badge/license-GPLv3-green)](./LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey)]()
[![GTK](https://img.shields.io/badge/GTK-4.0-8A2BE2)](https://www.gtk.org/)

---

> [!WARNING]
> **This project is in early development (v0.0.1).** The build system is fully functional, but the application itself is not yet complete. Use for development and learning purposes only.

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

## 📂 Project Structure

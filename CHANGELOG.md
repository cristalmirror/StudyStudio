# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

### Planned
- Implement CSS custom styles for widgets
- Add `GtkListView` / `GtkColumnView` for high-performance lists
- Support for modal dialogs and secondary windows
- Automated script to package Windows `.exe` with its DLLs
- Unit tests with GLib Testing Framework
- Internal API documentation
- Complete functional testing for both Linux and Windows builds
- User documentation and `README.md` improvements

---

## [0.0.1] - 2026-07-24

> [!WARNING]
> **This is a pre-release development version.** The build system is functional, but the application itself is not yet complete or fully tested. Use for development and testing purposes only.

### Added
- Cross-compilation setup for Linux and Windows using a single Docker environment (Fedora 40)
- GTK4 support for both platforms (native Linux + MinGW-w64 for Windows)
- Docker volume system to persist compiled binaries on the host
- `interface.ui` file with UI layout separated from C code using `GtkBuilder`
- Dynamic element addition functionality via button click
- Scrollable area (`GtkScrolledWindow`) to handle long lists of elements
- `AppState` structure to share state between GTK callbacks

### Changed
- Complete migration from GTK3 to GTK4, including:
  - Replacement of `gtk_container_add()` with `gtk_window_set_child()`
  - Removal of `gtk_widget_show_all()` in favor of `gtk_window_present()`
  - Use of `gtk_box_append()` to add widgets to containers
- Refactored visual design from hardcoded C to XML-based `.ui` files
- Separation of concerns: C code for logic, XML for presentation

### Fixed
- `PKG_CONFIG_PATH` conflict between Linux and Windows targets in Makefile
- Incorrect use of `GTK_WIDGET()` macro instead of `GTK_WINDOW()` for `gtk_window_set_application()`
- Inconsistency in `AppState` struct member names (`contenedor_destino` vs `target_box`)
- Docker creating fake directories when mounting non-existent files with `-v`
- "Device or resource busy" error when running `make clean` with mounted volumes
- "Failed to open display" error when running GTK applications inside Docker containers

### Known Issues
- Application is not yet fully functional
- Windows executable requires manual DLL copying for distribution
- No automated packaging system for Windows deployment
- Missing CSS styling for widgets
- No unit tests implemented

---

## Project Structure

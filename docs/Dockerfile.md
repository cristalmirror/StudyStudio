# `Dockerfile`

Status: draft for user approval; version 0.0.7.

## Purpose and configuration

Defines a Fedora 40 container image for compiling the application. Installs GCC/G++, Make, Git, pkg-config support, Linux GTK4/liblzma development packages, and MinGW-w64 compiler and GTK4/XZ packages. Cleans package-manager caches after installation.

Sets `/usr/src/app` as the working directory and copies the repository into the image. The default command is `/bin/bash`; building the image does not compile StudyStudio. A container invocation must run an appropriate Makefile target to produce binaries.

## Usage

From the project root, build the image with `docker build -t mi_app_builder .`. See the repository [README](../README.md#compilation) for the bind-mounted compilation command and output paths.

Package installation requires network access. Package versions are not pinned, so the file alone does not guarantee identical dependency versions on later image builds. This documentation does not verify package availability or either target's compilation. Application portability also depends on the source implementation.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep this file concerned with toolchain provisioning. Define application module boundaries in C interfaces and choose platform implementations through build configuration. Update installed dependencies when those implementations require them.

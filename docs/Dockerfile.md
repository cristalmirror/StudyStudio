# `Dockerfile`

Status: draft for user approval; version 0.0.13.

## Purpose and configuration

Defines a Fedora 40 container image for compiling the application. Installs GCC/G++, Make, Git, pkg-config support, Linux GTK4/liblzma development packages, and MinGW-w64 compiler and GTK4/XZ packages. Cleans package-manager caches after installation.

Sets `/usr/src/app` as the working directory and copies the repository into the image. The default command is `/bin/bash`; building the image does not compile StudyStudio. A container invocation must run an appropriate Makefile target to produce binaries.

As of version 0.0.13, a `RUN chown -R 1000:1000 /usr/src/app` step follows the `COPY`. `WORKDIR`/`COPY` run as root during `docker build`, so `/usr/src/app` itself is root-owned in the image regardless of what gets bind-mounted over its subdirectories at `docker run` time. Bind-mounting individual subdirectories (e.g. `-v "$(pwd)/build:/usr/src/app/build"`) gives those specific paths the host's ownership, but removing or creating an entry directly inside `/usr/src/app` (such as the `build` directory entry itself) needs write permission on `/usr/src/app`, not on the entry — so a non-root container invocation (`--user "$(id -u):$(id -g)"`) failed with "Permission denied" before this `chown`. Mounting the whole repository at `/usr/src/app` (`-v "$(pwd)":/usr/src/app`) sidesteps this on its own, since `/usr/src/app` is then the host directory rather than the image-baked one; the `chown` step exists for the per-subdirectory bind-mount style instead.

## Usage

From the project root, build the image with `docker build -t mi_app_builder .`. See the repository [README](../README.md#compilation) for the bind-mounted compilation command and output paths.

Package installation requires network access. Package versions are not pinned, so the file alone does not guarantee identical dependency versions on later image builds. This documentation does not verify package availability or either target's compilation. Application portability also depends on the source implementation.

## SOLID development direction

Follow the [project SOLID guidelines](README.md#development-direction-solid). Keep this file concerned with toolchain provisioning. Define application module boundaries in C interfaces and choose platform implementations through build configuration. Update installed dependencies when those implementations require them.

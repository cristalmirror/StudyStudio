# We use Fedora because it has precompiled GTK4 packages for MinGW-w64
FROM fedora:40

# Avoid interactive prompts and clean the cache to reduce image size
RUN dnf install -y \
    # --- Basic tools ---
    gcc gcc-c++ make git \
    # --- Compression and packaging libraries for Linux ---
    xz-devel libarchive-devel \
    # --- GTK4 libraries for Linux ---
    pkgconf-pkg-config gtk4-devel \
    # --- GTK4 libraries for Windows (cross-compilation) ---
    mingw64-gcc \
    mingw64-gtk4 \
    mingw64-xz \
    mingw64-libarchive \
    mingw64-pkg-config \
    && dnf clean all


WORKDIR /usr/src/app

# Copy the source code and the Makefile
COPY . .

# Let non-root users (e.g. --user "$(id -u):$(id -g)") write/remove files here,
# since bind mounts over individual subdirs don't change this directory's own ownership
RUN chown -R 1000:1000 /usr/src/app

# Keep the container alive so you can run 'make' inside it
CMD ["/bin/bash"]

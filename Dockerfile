# Usamos Fedora porque tiene paquetes precompilados de GTK4 para MinGW-w64
FROM fedora:40

# Evitar interacciones y limpiar caché para reducir el tamaño
RUN dnf install -y \
    # --- Herramientas básicas ---
    gcc gcc-c++ make git \
    # --- Librerías GTK4 para Linux ---
    pkgconf-pkg-config gtk4-devel \
    # --- Librerías GTK4 para Windows (Compilación cruzada) ---
    mingw64-gcc \
    mingw64-gtk4 \
    mingw64-pkg-config \
    && dnf clean all


WORKDIR /usr/src/app

# Copiamos el código y el Makefile
COPY . .

# Mantenemos el contenedor vivo para que tú ejecutes 'make'
CMD ["/bin/bash"]
# CLAUDE.md

Contexto del proyecto StudyStudio para Claude Code.

## Modo de trabajo (IMPORTANTE)

Claude es el **ayudante / colega** del desarrollador, no quien implementa.

- **No modificar archivos del proyecto por cuenta propia.** Nada de Edit/Write/sed
  sobre `src/`, `include/`, `Makefile`, `interface.ui`, docs, etc., salvo que el
  usuario lo pida explícitamente ("hazlo tú", "impleméntalo tú", "edítalo").
- Lo normal es: analizar, diagnosticar, explicar la causa raíz y **generar el
  código en el chat** para que el usuario lo revise y lo escriba a mano.
- Ciclo de trabajo: el usuario escribe el código → compila → Claude revisa y
  diagnostica → se corrige juntos → se vuelve a compilar.
- Explicar los cambios con detalle (como enseñando): el objetivo es que el
  usuario entienda C, GTK4 y liblzma, no solo tener un parche terminado.
- Leer archivos, buscar y compilar para verificar sí está permitido.
- No hacer commits ni push salvo que se pida.

## Qué es StudyStudio

App de escritorio en **C + GTK4** que gestiona "subjects" (materias) y archiva su
contenido en `.xz` con **liblzma**. Se compila para **Linux** y **Windows
(MinGW-w64)** con Docker + Makefile. Licencia GPLv3. Versión actual: ver `NAME`
en `Makefile` y `CHANGELOG.md`.

## Estructura

- `src/main.c` — punto de entrada, `AppState`, callbacks de GTK, logger del footer.
- `src/subject.c` — implementación de `Subject` (guardar/cargar `.xz`,
  extracción, `subject_log`). Tiene ramas `#ifdef _WIN32` separadas:
  - Linux: usa `tar` + liblzma.
  - Windows: recorrido nativo de directorios + stream LZMA propio (formato de
    entrada: `u32 len ruta + ruta + u64 tamaño + contenido`, sin versión ni
    marca de fin).
- `include/subject.h` — `struct Subject` con "métodos" como punteros a función
  (asignados en `new_subject`, llamados como `self->metodo(...)`),
  `SubjectLogFunc` / `subject_set_logger`.
- `interface.ui` + `resources.xml` — UI en XML (GtkBuilder), compilada a
  `build/resources.c` con `glib-compile-resources`.
- `docs/*.md` — documentación por archivo; `CHANGELOG.md` (Keep a Changelog).
- `.github/workflows/build.yml` — CI de release y debug.

## Compilación

El host no tiene `gtk4-devel`/`libarchive-devel`: compilar **dentro de Docker**
(imagen `mi_app_builder:latest`, Fedora 40, del `Dockerfile`).

```
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$(pwd)":/usr/src/app -w /usr/src/app \
  mi_app_builder:latest make linux win64
```

Targets: `linux`, `win64`, `all`, `debug`, `win64-debug`, `gdb` (en el host),
`clean`. Si se compila como root dentro del contenedor, `build/` queda con dueño
root y luego falla con `Permission denied`: usar `--user` o hacer
`chown -R 1000:1000 build` desde un contenedor.

## Convenciones

- Estilo del código: comentarios y mensajes en inglés, cabecera estándar en cada
  archivo C (Developer, Repository, Version, License, Last edited).
- Preferir llamadas estilo método (`self->fn(self, ...)`) sobre funciones
  estáticas sueltas en `Subject`.
- Commits: Conventional Commits con versión, p. ej.
  `feat (0.0.15): ...`, `fix (0.0.15): ...`, `docs (0.0.15): ...`.
- Al cambiar de versión: actualizar `Makefile` (`NAME`), cabeceras, `README.md`
  y `CHANGELOG.md`.
- Antes de retomar trabajo, leer `CHANGELOG.md` (`[Unreleased]` → Known Issues)
  y el `docs/` correspondiente.

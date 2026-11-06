# StudyStudio 0.1.4 — Informe de memoria, fugas y depuración

<a id="resumen"></a>
# 1. Portada / Resumen ejecutivo

**Fecha de las ejecuciones:** 6 de noviembre de 2026.  
**Versión:** 0.1.4, según `Makefile:2`.  
**Commit examinado:** `72a5cd3730bbe4a04b8055cef53cd081d874c4cc`.  
**Plataforma:** Linux, toolchain Docker del proyecto, GTK4 y C.  
**Herramientas:** AddressSanitizer + LeakSanitizer (ASan + LSan), Valgrind Memcheck/LeakCheck, GDB, tests GLib existentes y arneses temporales de estrés.  
**Estado de las correcciones:** ninguna aplicada. Los fragmentos de este documento son propuestas para revisar y validar; no son fixes probados.

**Se confirmaron cuatro defectos en el código de StudyStudio: dos altos y dos medios.** Para documentar todo el diagnóstico, este informe incluye además una fuga confirmada en Poppler, un warning de compilación y una aserción de Writer cuya causa sigue pendiente. Son **siete entradas de diagnóstico**, no siete defectos propios confirmados.

| Severidad | Defectos confirmados de StudyStudio | Otros hallazgos documentados | Total de entradas |
|---|---:|---:|---:|
| Crítico | 0 | 0 | 0 |
| Alto | 2 | 0 | 2 |
| Medio | 2 | 1, dependencia Poppler | 3 |
| Bajo | 0 | 1, warning | 1 |
| Informativo | 0 | 1, fallo Writer pendiente | 1 |
| **Total** | **4** | **3** | **7** |

Resumen de los resultados en siete puntos:

1. Un controlador GTK del PDF puede ejecutar callbacks con datos del editor ya liberados; ASan y Valgrind lo demostraron mediante una reproducción dirigida.
2. El guardado `.xz` puede anunciar éxito aunque falle el vaciado final del archivo en `fclose()`, con riesgo de pérdida de datos no advertida.
3. Los errores de escritura al guardar un subject dejan un descriptor de pipe abierto por intento; se verificaron tres pipes sin cerrar.
4. El pegado HTML de DOC usa un stream de salida abierto donde GIO exige uno cerrado; el texto no se pega y aparecen errores críticos de GLib.
5. Los 13 tests existentes pasaron funcionalmente con ASan; los 20 ciclos por formato completaron 120 aperturas renderizadas, y el principal editó y guardó TXT, MD, PDF y ODT.
6. Las fugas de anotaciones PDF se reprodujeron sin código StudyStudio y se atribuyeron a Poppler 24.02; las pérdidas externas de GTK/GIO/fontconfig/LibreOffice se conservaron por separado.
7. GDB recorrió arranque, edición, guardado y destrucción, además de `.xz`; Writer falló bajo Valgrind, y `_Exit` limita lo que demuestra una salida DOC sin reporte automático de LSan.

Los problemas se ordenan por prioridad de corrección. Las entradas P1–P4 son los defectos propios confirmados; P5 pertenece a una dependencia, P6 es un warning y P7 requiere investigación adicional. Los riesgos estáticos no reproducidos figuran al final y no incrementan este conteo.

<a id="contenido"></a>
# 2. Tabla de contenido

- [1. Portada / Resumen ejecutivo](#resumen)
- [2. Tabla de contenido](#contenido)
- [3. Problemas encontrados](#problemas)
  - [P1 · Alto · PDF: uso de memoria liberada en callbacks](#p1-pdf)
  - [P2 · Alto · Guardado `.xz`: falso éxito al fallar el cierre](#p2-cierre)
  - [P3 · Medio · Subject: fuga de descriptores en errores de guardado](#p3-pipes)
  - [P4 · Medio · DOC: pegado enriquecido con stream abierto](#p4-pegado)
  - [P5 · Medio · Poppler 24.02: fuga en anotaciones](#p5-poppler)
  - [P6 · Bajo · Parámetro `msg` sin uso](#p6-warning)
  - [P7 · Informativo · Writer: aserción bajo Valgrind](#p7-writer)
- [4. Preparación y comandos de reproducción](#preparacion)
- [5. Cobertura y resultados de las ejecuciones](#cobertura)
- [6. Cómo interpretar fugas y códigos de salida](#interpretacion)
- [7. Limitaciones y observaciones no confirmadas](#limites)
- [8. Orden de corrección y verificación posterior](#prioridades)
- [9. Evidencia completa y referencias](#evidencia)

<a id="problemas"></a>
# 3. Problemas encontrados

<a id="p1-pdf"></a>
## [ALTO] P1. PDF: callbacks con datos del editor ya liberados

**Herramienta que lo detectó:** ASan y Valgrind; arnés temporal de ciclo de vida.  
**Tipo:** `heap-use-after-free`; lectura y escritura inválidas tras la liberación.  
**Archivo(s) y línea(s):** `src/edit_subclass/editPDF.c:717`, conexiones en `:903,907–910`, liberación en `:84,951`; cierre base en `src/edit.c:49,54`; asignación del array en `src/edit_subclass/editPDF.c:130`.  
**Función(es) involucrada(s):** `_pdf_build_pages`, `_pdf_on_drag_begin`, `_pdf_free_pages`, `_pdf_finalize`, `_edit_close`, `_pdf_load`.  
**Estado / atribución:** confirmado en StudyStudio mediante una reproducción dirigida; no se demostró ese orden de eventos mediante interacción ordinaria del usuario.

### Qué está fallando (concepto)

El editor registra callbacks de dibujo y gestos GTK pasando un `EditPDFPage *` como `user_data`. Ese puntero pertenece al array `pdf->pages`, no al controlador GTK. Al cerrar el editor, el array se libera, pero las conexiones no se invalidan explícitamente.

Retirar un widget de su padre no garantiza su destrucción inmediata: una referencia adicional puede mantener vivos el área o el controlador. En ese caso el callback sigue conectado, pero sus datos ya no existen. El fallo no consiste en que GTK libere un GObject con referencias vivas; consiste en liberar datos C que todavía utilizan callbacks de GObjects vivos.

### Por qué falla

1. `_pdf_load()` reserva el array de páginas.
2. `_pdf_build_pages()` pasa cada `view` a `gtk_drawing_area_set_draw_func()` y a las señales `drag-begin`, `drag-update` y `drag-end`.
3. La reproducción conserva referencias al área y al gesto.
4. `_edit_close()` retira el widget; `_pdf_finalize()` libera las páginas y luego se libera el editor base.
5. El gesto conservado emite `drag-begin`.
6. `_pdf_on_drag_begin()` ejecuta `view->pdf` en la línea 717 y lee un bloque ya liberado.

ASan se detiene en la primera lectura. Valgrind continúa y registra siete contextos inválidos, incluyendo una escritura en `_pdf_set_current()` y lecturas del propio `EditPDF` ya liberado.

### Bloque(s) de código que fallan

**`src/edit_subclass/editPDF.c:898–910` — código observado:**

```c
    for (int i = 0; i < pdf->n_pages; i++) {
        EditPDFPage *view = &pdf->pages[i];
        view->area = gtk_drawing_area_new();
        gtk_widget_set_halign(view->area, GTK_ALIGN_CENTER);
        gtk_widget_add_css_class(view->area, "pdf-page");
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(view->area), _pdf_draw, view, NULL);

        GtkGesture *drag = gtk_gesture_drag_new();
        gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(drag), GDK_BUTTON_PRIMARY);
        g_signal_connect(drag, "drag-begin", G_CALLBACK(_pdf_on_drag_begin), view);
        g_signal_connect(drag, "drag-update", G_CALLBACK(_pdf_on_drag_update), view);
        g_signal_connect(drag, "drag-end", G_CALLBACK(_pdf_on_drag_end), view);
        gtk_widget_add_controller(view->area, GTK_EVENT_CONTROLLER(drag));   /* area owns it */
```

**`src/edit_subclass/editPDF.c:77–87` — código observado:**

```c
/* frees the page array; the widgets belong to pages_box, not to it */
static void _pdf_free_pages(EditPDF *pdf) {
    for (int i = 0; i < pdf->n_pages && pdf->pages != NULL; i++) {
        EditPDFPage *view = &pdf->pages[i];
        _pdf_drop_cache(view);
        if (view->page != NULL) g_object_unref(view->page);
    }
    free(pdf->pages);
    pdf->pages = NULL;
    pdf->n_pages = 0;
}
```

**`src/edit_subclass/editPDF.c:715–720` — código observado:**

```c
static void _pdf_on_drag_begin(GtkGestureDrag *gesture, double x, double y, gpointer user_data) {
    EditPDFPage *view = user_data;
    EditPDF *pdf = view->pdf;
    double z = pdf->zoom;

    if (pdf->tool != EDIT_PDF_TOOL_VIEW) _pdf_set_current(pdf, view->index);
```

**`src/edit.c:38–54` — código observado:**

```c
    if (self->widget != NULL) {
        GtkWidget *widget = self->widget;
        self->set_widget(self, NULL);   /* drop the weak pointer before free(self) */
        GtkWidget *parent = gtk_widget_get_parent(widget);
        if (parent != NULL && GTK_IS_BOX(parent)) {
            gtk_box_remove(GTK_BOX(parent), widget);
        }
    }

    /* Let the subclass free its own fields (virtual destructor) */
    if (self->finalize != NULL) {
        self->finalize(self);
    }

    /* Free the base fields and the whole object */
    g_free(self->path); 
    free(self);
```

El orden de limpieza está documentado también en `_pdf_finalize():949–962`: su primera operación es `_pdf_free_pages(pdf)`, sin desconectar antes esos callbacks. El comentario de `_edit_close()` que supone que retirar el widget impide señales no cubre las referencias que lo mantienen vivo.

### Evidencia (salida de la herramienta)

Extractos literales de `asan-pdf-retained.log`, con omisiones señaladas:

```text
pdf-retained: emit drag-begin on live controller after editor close
=================================================================
==1365==ERROR: AddressSanitizer: heap-use-after-free on address 0x50800000fe20 at pc 0x000000419c55 bp 0x7fff8811c6d0 sp 0x7fff8811c6c8
READ of size 8 at 0x50800000fe20 thread T0
    #0 0x419c54 in _pdf_on_drag_begin src/edit_subclass/editPDF.c:717
```

```text
freed by thread T0 here:
    #0 0x7fbc8adf8638 in free.part.0 (/lib64/libasan.so.8+0xf6638) (BuildId: a4ad7eb954b390cf00f07fa10952988a41d9fc7a)
    #1 0x417735 in _pdf_free_pages src/edit_subclass/editPDF.c:84
    #2 0x418c66 in _pdf_finalize src/edit_subclass/editPDF.c:951
    #3 0x40e00d in _edit_close src/edit.c:49
```

Valgrind confirma la misma dirección de memoria y su liberación:

```text
==1960== Invalid read of size 8
==1960==    at 0x415F49: _pdf_on_drag_begin (editPDF.c:717)
```

Los stacks completos de asignación, liberación y uso están en [ASan PDF retenido](/tmp/studystudio-audit-20261106/asan-pdf-retained.log) y [Valgrind PDF retenido](/tmp/studystudio-audit-20261106/valgrind-pdf-retained.raw.log).

### Dónde encontrarlo / Cómo reproducirlo

Primero seguir la [preparación del contenedor y displays](#preparacion). Después, desde la raíz del repositorio:

```bash
docker exec --user 1000:1000 \
  -e ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:malloc_context_size=12 \
  studystudio-memory-audit bash /audit/gtk-env.sh \
  /audit/editor-asan pdf-retained
```

El modo `pdf-retained` de `/audit/editor_stress.c:144–174` crea un PDF, lo renderiza, mantiene referencias al área y al `GtkGestureDrag`, cierra el editor y emite el gesto sobre el objeto vivo. No requiere acciones manuales ni documentos del usuario. La salida esperada antes de corregir es el UAF y código 1.

Para comparar bajo Valgrind:

```bash
docker exec --user 1000:1000 studystudio-memory-audit \
  bash /audit/valgrind-env.sh pdf-retained-repro \
  /audit/editor-debug pdf-retained
```

### Impacto

Puede producir crash o corrupción de memoria si un callback llega después de liberar el editor o el array de páginas. Las referencias externas reproducen el orden de vida peligroso; la frecuencia de ese orden en uso ordinario no se estableció. Las 120 aperturas renderizadas normales no demostraron este fallo por sí solas.

### Sugerencia de corrección (sin aplicar)

**Propuesta no aplicada ni validada:** invalidar conexiones y función de dibujo antes de liberar o reemplazar las páginas. La limpieza debe poder actuar tanto si el widget sigue vivo como si GTK lo destruyó antes que el editor.

Una opción es añadir un puntero débil al área y una referencia propia al gesto en `EditPDFPage`:

```c
/* Proposed fields; not applied. */
GtkWidget *area;   /* weak pointer */
GtkGesture *drag;  /* owned reference */
```

Al construir la página, registrar el área débil, conservar una referencia al gesto y transferir la referencia original al widget. Luego usar una limpieza equivalente a esta **antes** de liberar `view`:

```c
/* Proposed helper; requires the fields and ownership setup above. */
static void _pdf_unbind_page(EditPDFPage *view) {
    if (view->drag != NULL) {
        g_signal_handlers_disconnect_by_data(view->drag, view);
        g_clear_object(&view->drag);
    }
    if (view->area != NULL) {
        gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(view->area),
                                      NULL, NULL, NULL);
        g_signal_handlers_disconnect_by_data(view->area, view);
        g_object_remove_weak_pointer(G_OBJECT(view->area),
                                    (gpointer *)&view->area);
        view->area = NULL;
    }
}
```

Integrarlo en `_pdf_free_pages()` y al reconstruir widgets: `_pdf_load():141` también reemplaza el array cuando cambia el número de páginas. Quitar la inscripción del puntero débil antes de liberar el array evita que GTK escriba después en memoria liberada. Los botones de la barra PDF también llevan callbacks con `pdf` como dato (`:859,881–882`); deben quedar cubiertos por la política de desconexión, aunque esa variante no se reprodujo por separado.

Después del cambio, repetir el gesto retenido, ambos órdenes de destrucción ventana/editor y los ciclos renderizados. Conservar solo el controlador, sin el área, debe ser otro caso de verificación.

<a id="p2-cierre"></a>
## [ALTO] P2. Guardado `.xz`: éxito anunciado aunque falla el cierre del archivo

**Herramienta que lo detectó:** Tests del arnés Subject ejecutados con ASan y con Valgrind.  
**Tipo:** error lógico y de manejo de I/O; falso éxito al guardar. No es un acceso inválido detectado por ASan.  
**Archivo(s) y línea(s):** `src/subject.c:981`, mensajes de éxito en `:993–994`.  
**Función(es) involucrada(s):** `_save_subject`, `_wait_pid_os_opt`, `subject_log`.  
**Estado / atribución:** confirmado en StudyStudio con salida `/dev/full` y un subject pequeño.

### Qué está fallando (concepto)

El código considera terminado el guardado tras generar el stream LZMA y esperar a `tar`, pero ignora el resultado de `fclose(outf)`. En stdio, una escritura puede quedar en el buffer del proceso; el fallo real puede aparecer recién cuando el cierre intenta vaciarlo al dispositivo.

Un `fwrite()` exitoso no basta para anunciar que se terminó de escribir el archivo. En esta reproducción el dispositivo rechaza los datos, pero el usuario recibe el mensaje de creación exitosa.

### Por qué falla

1. El arnés crea una materia pequeña y usa `/dev/full` como ruta de salida.
2. El encoder produce pocos bytes; stdio los admite en su buffer.
3. El stream LZMA llega a `LZMA_STREAM_END`, por lo que se sale del bucle normal.
4. `fclose()` intenta escribir el buffer y falla por falta de espacio; su retorno se descarta.
5. `tar` ha terminado correctamente, así que la comprobación de su estado no detecta el error de escritura del padre.
6. Se imprimen `Created /dev/full` y el mismo mensaje para el logger.

### Bloque(s) de código que fallan

**`src/subject.c:980–995` — código observado:**

```c
    lzma_end(&strm);
    fclose(outf);
    close(pipefd[0]);

    int status;
    if (self->wait_pid_os_opt(self, &status) == -1) {
        return;
    }
    if (status != 0) {
        fprintf(stderr, "tar failed\n");
        return;
    }

    printf("Created %s\n", *outpath);
    subject_log("Created %s\n", *outpath);
}
```

La espera del hijo comprueba la creación del TAR, no el cierre del archivo `.xz` que escribe el proceso padre. Son resultados distintos que deben conservarse y evaluarse por separado.

### Evidencia (salida de la herramienta)

Extracto literal de `asan-subject.log`:

```text
Created /dev/full
SUBJECT: Created /dev/full
FAIL: buffered tiny save detects /dev/full failure at fclose
SUMMARY checks=63 failures=4; fixtures retained at /tmp/studystudio-subject-audit-OYrHMd
[exit 1]
```

El mismo comportamiento se observó en [el arnés bajo Valgrind](/tmp/studystudio-audit-20261106/valgrind-subject.log). La evidencia demuestra un error funcional, aunque el heap del arnés se libere por completo.

### Dónde encontrarlo / Cómo reproducirlo

Después de la [preparación común](#preparacion):

```bash
docker exec --user 1000:1000 \
  -e ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  studystudio-memory-audit /audit/subject-asan
```

El arnés crea sus propios fixtures. Al final de `/audit/subject_stress.c` genera el subject `tiny`, escribe un TXT de cinco bytes y llama `save_subject(..., "/dev/full")`. No necesita display. El proceso termina en 1 porque sus comprobaciones señalan los fallos de P2 y P3; no es un test limpio esperado.

No usar archivos del usuario para provocar falta de espacio. `/dev/full` dentro del contenedor es el dispositivo de prueba utilizado.

### Impacto

El usuario puede creer que el subject quedó guardado cuando el archivo resultante está vacío o incompleto. El defecto puede manifestarse ante errores diferidos del sistema de archivos o del dispositivo de salida. No se demostró corrupción silenciosa del encoder: lo confirmado es la pérdida del diagnóstico de escritura y el falso mensaje de éxito.

### Sugerencia de corrección (sin aplicar)

**Propuesta no aplicada:** sustituir el cierre normal por uno comprobado y emitir éxito solamente si tanto el cierre como la espera del hijo fueron satisfactorios. Conservar `errno` del cierre inmediatamente; `waitpid()` u otras operaciones pueden cambiarlo.

```c
/* Proposed replacement of the normal cleanup/success tail. */
lzma_end(&strm);
int close_error = fclose(outf) == EOF ? errno : 0;
outf = NULL;
close(pipefd[0]);
pipefd[0] = -1;

int status = 1;
int wait_rc = self->wait_pid_os_opt(self, &status);
if (close_error != 0) {
    subject_log("Could not finish writing %s: %s\n",
                *outpath, strerror(close_error));
}
if (close_error != 0 || wait_rc != 0 || status != 0) return;

printf("Created %s\n", *outpath);
subject_log("Created %s\n", *outpath);
```

El helper de espera retorna 0/-1 por éxito/fallo de la API y normaliza `status` a 0/1; no entrega el estado bruto de `waitpid`. No ocultar el error de cierre si además falla la espera.

Como mejora posterior, evaluar una API de guardado que devuelva un resultado y un archivo temporal vecino que solo se renombre tras completar todas las comprobaciones. Eso requiere revisar los callers; no está implementado aquí. Verificar guardados normales, `/dev/full` pequeño y grande y una ruta no escribible.

<a id="p3-pipes"></a>
## [MEDIO] P3. Subject: se pierde un descriptor de pipe por guardado fallido

**Herramienta que lo detectó:** Valgrind con `--track-fds=yes` y conteo de descriptores del arnés ejecutado con ASan.  
**Tipo:** fuga de recursos del sistema operativo, específicamente descriptores de archivo; no es una fuga de heap de LSan.  
**Archivo(s) y línea(s):** `src/subject.c:802`; retorno probado en `:940–949`; otros retornos equivalentes en `:884–893`, `:905–914` y `:956–965`; cierre normal en `:982`.  
**Función(es) involucrada(s):** `_save_subject`, `_wait_pid_os_opt`.  
**Estado / atribución:** confirmado en StudyStudio para la rama de error de `fwrite()`.

### Qué está fallando (concepto)

El padre es propietario del extremo de lectura `pipefd[0]` por el que recibe el TAR del hijo. El código lo cierra en el camino normal, pero varias ramas de error retornan sin cerrarlo.

Liberar el encoder y cerrar el `FILE *` de salida no libera el descriptor independiente del pipe. Tampoco lo libera esperar al hijo: el descriptor pertenece al proceso padre.

### Por qué falla

1. `pipe()` crea dos descriptores y `fork()` inicia el productor TAR.
2. El padre cierra `pipefd[1]` y sigue usando `pipefd[0]`.
3. Al guardar datos grandes en `/dev/full`, `fwrite()` falla durante el bucle.
4. La rama libera LZMA, cierra el archivo de salida, termina/recolecta al hijo y retorna.
5. No pasa por el `close(pipefd[0])` de la línea 982.
6. Cada nuevo intento repite la pérdida de un descriptor.

### Bloque(s) de código que fallan

**`src/subject.c:801–804` — código observado:**

```c
    int pipefd[2];
    if (pipe(pipefd) == -1) self->fatal("pipe");
    self->pid = fork();
    if (self->pid == -1) self->fatal("fork");
```

**`src/subject.c:938–950` — código observado:**

```c
        size_t wrote = OUT_BUF_SIZE - strm.avail_out;
        if (wrote > 0) {
            if (fwrite(outbuf, 1, wrote, outf) != wrote) {
                perror("fwrite exit xz");
                lzma_end(&strm);
                fclose(outf);
                kill(self->pid, SIGTERM);
                if (self->wait_pid_os_opt(self,NULL) == -1) {
                    fprintf(stderr,"Error in the request %d\n",-1);
                    subject_log("Error in the request %d\n",-1);
                } 
                return;
            }
```

Comparar con el camino normal:

**`src/subject.c:980–982` — código observado:**

```c
    lzma_end(&strm);
    fclose(outf);
    close(pipefd[0]);
```

La rama de fallo de `fopen():870–879` termina mediante `_fatal()` y `exit()`. El sistema operativo cierra entonces los descriptores; no se presenta como otra fuga progresiva en un proceso que continúa vivo. La demostración de P3 corresponde a los retornos normales de error indicados.

### Evidencia (salida de la herramienta)

El arnés observó incrementos sucesivos de descriptores:

```text
fwrite exit xz: No space left on device
FAIL: failed large save closes descriptors attempt=1 before=4 after=5
```

```text
FAIL: failed large save closes descriptors attempt=2 before=5 after=6
```

```text
FAIL: failed large save closes descriptors attempt=3 before=6 after=7
```

Valgrind localiza el origen; extractos literales de `valgrind-subject.raw.log`:

```text
==1345== FILE DESCRIPTORS: 6 open (3 std) at exit.
==1345== Open file descriptor 5:
==1345==    at 0x49A814D: pipe (in /usr/lib64/libc.so.6)
==1345==    by 0x403876: _save_subject (subject.c:802)
==1345==    by 0x40351B: main (subject_stress.c:210)
```

```text
==1345==     in use at exit: 0 bytes in 0 blocks
==1345==   total heap usage: 721 allocs, 721 frees, 1,602,054,119 bytes allocated
```

El reporte completo enumera también los descriptores 4 y 3. Los números iniciales difieren bajo Valgrind porque la instrumentación abre sus propios descriptores; interesa el incremento y el stack de `pipe()`.

### Dónde encontrarlo / Cómo reproducirlo

Tras iniciar el contenedor, no hace falta display:

```bash
docker exec --user 1000:1000 studystudio-memory-audit \
  valgrind --leak-check=full --show-leak-kinds=all \
  --track-origins=yes --error-exitcode=1 --num-callers=30 \
  --track-fds=yes /audit/subject-debug
```

El arnés crea un archivo aleatorio de 8 MiB y repite tres guardados fallidos en `/dev/full`. Buscar `Open file descriptor` y `_save_subject (subject.c:802)` en la salida final. La misma ejecución también muestra P2.

### Impacto

Una sucesión de guardados fallidos acumula descriptores hasta poder impedir abrir más archivos, pipes u otros recursos. El caso normal no mostró crecimiento en diez ciclos; el fallo está en el cleanup de error.

### Sugerencia de corrección (sin aplicar)

**Propuesta mínima no aplicada:** cerrar el extremo de lectura antes de retornar en cada rama que mantiene vivo al padre. Para el camino demostrado:

```diff
             if (fwrite(outbuf, 1, wrote, outf) != wrote) {
                 perror("fwrite exit xz");
                 lzma_end(&strm);
+                close(pipefd[0]);
                 fclose(outf);
                 kill(self->pid, SIGTERM);
```

Aplicar la misma política a los errores de encoder, `read()` y `lzma_code()`. Este fragmento no es un patch completo de todas las ramas.

La solución más mantenible es una salida común de cleanup con descriptores inicialmente en `{-1, -1}` y ownership explícito. Cerrar cada extremo una sola vez, marcarlo como `-1`, finalizar LZMA cuando corresponda, comprobar el cierre de salida y recolectar al hijo incluso ante fallo. Integrar el manejo de P2 para evitar que las dos correcciones introduzcan cierres dobles. Validar que tres fallos consecutivos mantienen constante el número de FD.

<a id="p4-pegado"></a>
## [MEDIO] P4. DOC: pegado enriquecido intenta leer un stream de salida abierto

**Herramienta que lo detectó:** Tests del arnés DOC bajo ASan; GDB con `G_DEBUG=fatal-criticals`.  
**Tipo:** violación del contrato de GIO y error lógico; `GBytes *` nulo seguido de llamadas rechazadas por GLib. No se confirmó un acceso inválido a NULL de CPU.  
**Archivo(s) y línea(s):** `src/edit_subclass/editDOCOps.c:174–175,193–194`; selección de la rama enriquecida en `:211–217`.  
**Función(es) involucrada(s):** `_doc_paste`, `_doc_paste_read_done`, `_doc_paste_splice_done`, `_doc_paste_data`.  
**Estado / atribución:** confirmado en StudyStudio con contenido `text/html`.

### Qué está fallando (concepto)

La operación asíncrona copia el clipboard a un `GMemoryOutputStream`, pero solo cierra el stream de origen. El callback intenta extraer los bytes del destino sin cerrarlo primero.

`g_memory_output_stream_steal_as_bytes()` exige que el stream esté cerrado. La función emite un critical y devuelve NULL; el código pasa ese NULL a las funciones de `GBytes`, que también emiten criticals. El resultado funcional es que el texto no se pega.

### Por qué falla

1. `_doc_paste(edit, false)` ve que el clipboard ofrece HTML y solicita el stream.
2. `_doc_paste_read_done()` crea un destino de memoria.
3. `g_output_stream_splice_async()` recibe únicamente `G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE`.
4. La copia termina, pero el destino sigue abierto.
5. `_doc_paste_splice_done()` llama a `steal_as_bytes()` y obtiene NULL.
6. La llamada a `_doc_paste_data()` no recibe contenido válido; la selección posterior queda vacía.

La misma rama acepta `image/png`; este camino también debe verificarse al implementar el fix, aunque la reproducción de la auditoría usó HTML. El pegado de texto plano utiliza otro callback, por eso los tests existentes podían pasar sin encontrar P4.

### Bloque(s) de código que fallan

**`src/edit_subclass/editDOCOps.c:170–180` — código observado:**

```c
static void _doc_paste_splice_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    DocPaste *job = user_data;
    GOutputStream *out = G_OUTPUT_STREAM(source);
    if (g_output_stream_splice_finish(out, res, NULL) >= 0) {
        GBytes *bytes = g_memory_output_stream_steal_as_bytes(G_MEMORY_OUTPUT_STREAM(out));
        _doc_paste_data(job, g_bytes_get_data(bytes, NULL), g_bytes_get_size(bytes));
        g_bytes_unref(bytes);
    }
    g_object_unref(out);
    _doc_paste_free(job);
}
```

**`src/edit_subclass/editDOCOps.c:190–196` — código observado:**

```c
    g_free(job->mime);
    job->mime = g_strdup(mime);
    GOutputStream *out = g_memory_output_stream_new_resizable();
    g_output_stream_splice_async(out, in, G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
                                 G_PRIORITY_DEFAULT, NULL, _doc_paste_splice_done, job);
    g_object_unref(in);
}
```

### Evidencia (salida de la herramienta)

Extracto literal de `asan-doc-rich.log`:

```text
doc-rich: requesting HTML paste (plain_text=false)

(process:1487): GLib-GIO-CRITICAL **: 22:41:13.937: g_memory_output_stream_steal_as_bytes: assertion 'g_output_stream_is_closed (G_OUTPUT_STREAM (ostream))' failed

(process:1487): GLib-CRITICAL **: 22:41:13.937: g_bytes_get_size: assertion 'bytes != NULL' failed

(process:1487): GLib-CRITICAL **: 22:41:13.937: g_bytes_get_data: assertion 'bytes != NULL' failed
doc-rich: selection=(null); pasted=false
```

Al convertir criticals en señales, GDB se detiene en el punto de fallo:

```text
Thread 1 "editor-debug" received signal SIGTRAP, Trace/breakpoint trap.
0x00007f62ba58cde8 in g_logv () from /lib64/libglib-2.0.so.0
#0  0x00007f62ba58cde8 in g_logv () at /lib64/libglib-2.0.so.0
#1  0x00007f62ba58d063 in g_log () at /lib64/libglib-2.0.so.0
#2  0x00007f62ba913e42 in g_memory_output_stream_steal_as_bytes () at /lib64/libgio-2.0.so.0
#3  0x0000000000412be9 in _doc_paste_splice_done (source=0x278836a0, res=0x278836d0, user_data=0x2782bdb0) at src/edit_subclass/editDOCOps.c:174
```

### Dónde encontrarlo / Cómo reproducirlo

Después de la [preparación GTK](#preparacion):

```bash
docker exec --user 1000:1000 \
  -e ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:malloc_context_size=12 \
  -e AUDIT_LSAN_CHECK=1 studystudio-memory-audit \
  bash /audit/gtk-env.sh /audit/editor-asan doc-rich
```

El arnés abre Writer con `private:factory/swriter`, establece un proveedor de clipboard `text/html`, solicita `paste(edit, false)`, procesa el mainloop, selecciona el documento y verifica si apareció el texto. Antes del fix devuelve 2 por pegado fallido.

Para detenerse con GDB y ver las variables del callback:

```bash
docker exec --user 1000:1000 -e G_DEBUG=fatal-criticals \
  studystudio-memory-audit bash /audit/gtk-env.sh \
  gdb -q -batch -ex 'set pagination off' \
  -ex 'set debuginfod enabled off' -ex run -ex 'bt 20' \
  -ex 'frame 3' -ex 'info locals' --args /audit/editor-debug doc-rich
```

La auditoría capturó este backtrace en [gdb-doc-critical-backtrace.log](/tmp/studystudio-audit-20261106/gdb-doc-critical-backtrace.log). El `SIGTRAP` depende de `fatal-criticals`; no se informa como un crash espontáneo del modo normal.

### Impacto

El pegado enriquecido deja de funcionar y puede aparentar que se aceptó la operación inicial porque la copia es asíncrona. Con criticals fatales la aplicación se detiene. El informe no atribuye a este fallo las fugas externas que también aparecen al iniciar LibreOffice.

### Sugerencia de corrección (sin aplicar)

**Diff propuesto, no aplicado ni validado:** cerrar también el destino al finalizar la copia.

```diff
-    g_output_stream_splice_async(out, in, G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE,
+    g_output_stream_splice_async(out, in,
+                                 G_OUTPUT_STREAM_SPLICE_CLOSE_SOURCE |
+                                 G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                  G_PRIORITY_DEFAULT, NULL, _doc_paste_splice_done, job);
```

Mantener los `unref` existentes: la corrección consiste en cerrar el stream, no en incrementar su liberación. Como mejora, recoger y notificar el `GError` de `splice_finish()` en lugar de ocultar una lectura/copia fallida.

Validar HTML, PNG y texto plano, y cerrar el editor mientras una lectura de clipboard sigue pendiente. El [contrato oficial de GIO](https://docs.gtk.org/gio/method.MemoryOutputStream.steal_as_bytes.html) exige el destino cerrado antes de extraer sus bytes.

<a id="p5-poppler"></a>
## [MEDIO] P5. Poppler 24.02: fuga de las anotaciones PDF creadas

**Herramienta que lo detectó:** LSan y Valgrind, con reproducción independiente de StudyStudio y revisión de la fuente oficial de Poppler.  
**Tipo:** `memory leak` en una dependencia; referencias C++ de anotaciones sin balancear.  
**Archivo(s) y línea(s):** origen externo en Poppler 24.02.0, `glib/poppler-annot.cc:186,196,248–250`; inicialización de contador en `poppler/Annot.cc:1267`. Callers del proyecto: `src/edit_subclass/editPDF.c:258–259,297,308,322`.  
**Función(es) involucrada(s):** `poppler_annot_text_new`, `poppler_annot_square_new`, `poppler_annot_text_markup_new_highlight`, `_poppler_create_annot`, `poppler_annot_finalize`; `_pdf_add_annot` es caller de StudyStudio.  
**Estado / atribución:** fuga confirmada en la dependencia del contenedor; no es un `g_object_unref()` faltante del proyecto.

### Qué está fallando (concepto)

Hay dos niveles de recursos: el wrapper GObject de la API GLib y la anotación C++ interna. StudyStudio libera su referencia al wrapper correctamente. El constructor antiguo de Poppler deja una referencia inicial del objeto C++ que no se compensa al envolverlo.

Que aparezca un frame `_pdf_add_note()` en un stack de asignación no demuestra por sí solo que StudyStudio sea dueño de la referencia perdida. La reproducción mínima sin código del proyecto es la evidencia que separa las responsabilidades.

### Por qué falla

1. La anotación C++ se construye con contador inicial 1.
2. El wrapper de Poppler incrementa ese contador.
3. Las referencias de la página y el wrapper se liberan al limpiar sus objetos correspondientes.
4. La referencia inicial que el constructor no entrega sigue sin balancear.

Además se observaron asignaciones de cuadriláteros dentro de `poppler_page_add_annot()`. La versión 26.07 examinada usa `shared_ptr`/`make_shared`; esta diferencia de implementación no certifica que esa versión esté libre de todas las fugas posibles.

### Bloque(s) de código que fallan

El caller de StudyStudio ya libera el wrapper:

**`src/edit_subclass/editPDF.c:257–261` — código observado:**

```c
static void _pdf_add_annot(EditPDFPage *view, PopplerAnnot *annot) {
    poppler_page_add_annot(view->page, annot);
    g_object_unref(annot);                     /* the page keeps its own ref */
    _pdf_repaint(view);
}
```

Estas son las líneas exactas relevantes de la fuente externa descargada, sin reproducir el resto de la biblioteca:

**`poppler-24.02.0/glib/poppler-annot.cc:248–248` — código observado:**

```cpp
    annot = new AnnotText(doc->doc, &pdf_rect);
```

**`poppler-24.02.0/glib/poppler-annot.cc:250–250` — código observado:**

```cpp
    return _poppler_annot_text_new(annot);
```

**`poppler-24.02.0/glib/poppler-annot.cc:186–186` — código observado:**

```cpp
    annot->incRefCnt();
```

**`poppler-24.02.0/glib/poppler-annot.cc:196–196` — código observado:**

```cpp
        poppler_annot->annot->decRefCnt();
```

Las rutas de Poppler anteriores corresponden a los archivos oficiales conservados bajo `/tmp/studystudio-audit-20261106/poppler-24.02.0/`, no al árbol `src/` de StudyStudio.

### Evidencia (salida de la herramienta)

El mínimo `/audit/poppler-minimal` enlaza Poppler y Cairo, sin GTK ni objetos de StudyStudio. LSan reportó:

```text
Direct leak of 912 byte(s) in 3 object(s) allocated from:
    #0 0x7f47200c34b8 in operator new(unsigned long) (/lib64/libasan.so.8+0xf84b8) (BuildId: a4ad7eb954b390cf00f07fa10952988a41d9fc7a)
    #1 0x7f471ff892e9 in poppler_annot_text_new (/lib64/libpoppler-glib.so.8+0x382e9) (BuildId: d0031f2b4ffe9dbab7da1c5d85248e4138f99259)
    #2 0x4015b5 in main /audit/poppler_minimal.c:28
```

```text
SUMMARY: AddressSanitizer: 3402 byte(s) leaked in 42 allocation(s).
```

En los tests PDF originales, el total inicial de 7087 bytes / 91 asignaciones se separó así: 6212 bytes / 84 de anotaciones Poppler, 363 bytes / 5 de fontconfig y 512 bytes / 2 de Mesa. El mínimo independiente de Valgrind también mostró pérdidas, aunque sus totales no son idénticos a los del mínimo ASan.

### Dónde encontrarlo / Cómo reproducirlo

No necesita display. Con el contenedor iniciado:

```bash
docker exec --user 1000:1000 \
  -e ASAN_OPTIONS=detect_leaks=1:malloc_context_size=10 \
  studystudio-memory-audit /audit/poppler-minimal 3
```

El programa crea un PDF vacío sin fuentes, añade tres notas, libera anotaciones/página/documento y retorna para que LSan analice las pérdidas. Su fuente está en [poppler_minimal.c](/tmp/studystudio-audit-20261106/poppler_minimal.c); el [reporte aislado](/tmp/studystudio-audit-20261106/asan-poppler-isolated.log) registra también su comando de compilación exacto.

### Impacto

Crear anotaciones acumula recursos internos de Poppler en esta versión y contamina los resultados LSan de la aplicación. Es distinto de los datos de página liberados demasiado pronto de P1: una fuga de dependencia no elimina ni explica ese UAF propio.

### Sugerencia de corrección (sin aplicar)

**Propuesta para la dependencia, sin aplicar:** evaluar una actualización de Poppler o un backport del manejo de referencias, y volver a ejecutar el mínimo y los tests del editor en la versión elegida. El análisis no determina la primera versión que corrige todos los casos.

Preservar el `g_object_unref(annot)` de StudyStudio; agregar un segundo unref usaría un wrapper destruido. El cambio de ownership debe hacerse en la biblioteca y según su versión, no mediante un cast o liberación adicional desde StudyStudio.

```c
/* Keep the existing application-side ownership balance. */
poppler_page_add_annot(view->page, annot);
g_object_unref(annot);
```

Referencias: [fuente oficial Poppler 24.02.0](https://poppler.freedesktop.org/poppler-24.02.0.tar.xz), [fuente oficial 26.07.0](https://poppler.freedesktop.org/poppler-26.07.0.tar.xz) y [análisis de atribución](/tmp/studystudio-audit-20261106/pdf-attribution.md).

<a id="p6-warning"></a>
## [BAJO] P6. Parámetro `msg` sin uso en el guardado Linux

**Herramienta que lo detectó:** compilación GCC con `-Wall -Wextra`.  
**Tipo:** warning `-Wunused-parameter`; no es un error de memoria.  
**Archivo(s) y línea(s):** `src/subject.c:799`.  
**Función(es) involucrada(s):** `_save_subject`, variante Linux.  
**Estado / atribución:** warning confirmado, sin fallo de ejecución asociado demostrado.

### Qué está fallando (concepto)

La firma acepta `msg`, pero la implementación Linux no lo utiliza. La advertencia puede representar un parámetro heredado o una parte del contrato que todavía no se implementó. La ejecución de memoria no determinó que ese parámetro deba poseer o liberar un recurso.

### Por qué falla

GCC compila la rama Linux con `-Wextra`, detecta un argumento formal que no se lee y emite la advertencia. Se ve al compilar objetos de debug y de tests; es el mismo warning repetido por dos compilaciones.

### Bloque(s) de código que fallan

**`src/subject.c:798–804` — código observado:**

```c
/* compress and save all archives and information */
void _save_subject(Subject *self, char **msg, const char **dir, const char **outpath) {

    int pipefd[2];
    if (pipe(pipefd) == -1) self->fatal("pipe");
    self->pid = fork();
    if (self->pid == -1) self->fatal("fork");
```

### Evidencia (salida de la herramienta)

```text
src/subject.c: In function '_save_subject':
src/subject.c:799:42: warning: unused parameter 'msg' [-Wunused-parameter]
  799 | void _save_subject(Subject *self, char **msg, const char **dir, const char **outpath) {
      |                                   ~~~~~~~^~~
```

### Dónde encontrarlo / Cómo reproducirlo

Buscar ese texto en [build-asan.log](/tmp/studystudio-audit-20261106/build-asan.log) o [build-debug.log](/tmp/studystudio-audit-20261106/build-debug.log). Para forzar su compilación con las mismas variables temporales, seguir la preparación y ejecutar:

```bash
docker exec --user 1000:1000 studystudio-memory-audit \
  bash -lc 'flags="-MMD -MP -Wall -Wextra -g -O0 -DDEBUG -Iinclude -Iinclude/third_party $(pkgconf --cflags gtk4 poppler-glib gmodule-2.0)"; make build/report-warning-repro/linux-debug/subject.o BUILD_DIR=build/report-warning-repro "CFLAGS_LINUX_DEBUG=$flags"'
```

Esta receta propuesta usa un directorio nuevo para compilar solo el objeto de Subject; si ya existe, `make` puede considerarlo actualizado. No se ejecutó durante la redacción y no hace falta para consultar la evidencia conservada. No modifica el Makefile ni silencia warnings globales.

### Impacto

Genera ruido de compilación y puede ocultar la intención de la API. No se observó un crash, fuga o pérdida de datos atribuible a `msg` sin uso. Los errores de guardado P2 y P3 tienen causas independientes.

### Sugerencia de corrección (sin aplicar)

Primero decidir si `msg` debe transportar un resultado. Si el contrato requiere mantener la firma y el argumento es intencionalmente reservado, una propuesta mínima sería:

```c
/* Proposed only after confirming the parameter is intentionally unused. */
(void)msg;
```

Si debe informar errores, implementarlo con ownership documentado en lugar de ocultar el warning. Quitar el argumento requiere revisar el puntero de función de `Subject` y sus callers. Ninguna de estas opciones fue aplicada.

<a id="p7-writer"></a>
## [INFORMATIVO] P7. Writer: aserción de estado de tabla bajo Valgrind

**Herramienta que lo detectó:** Tests ejecutados bajo Valgrind.  
**Tipo:** fallo de aserción funcional; causa pendiente. No es un UAF, fuga o timeout confirmado.  
**Archivo(s) y línea(s):** `tests/test_edit_doc.c:112–114`; espera fija en `:30–33`.  
**Función(es) involucrada(s):** `test_writer`, `settle`, operaciones de inserción de tabla de EditDOC y callbacks de estado de LibreOffice.  
**Estado / atribución:** observado en la suite completa bajo Valgrind; no reproducido en los 13 tests funcionales con ASan. No se asigna una severidad de producción sin conocer la causa.

### Qué está fallando (concepto)

El test solicita crear una tabla y después espera que `doc->in_table` sea verdadero. Bajo Valgrind ese estado seguía siendo falso cuando se ejecutó la aserción.

La solicitud inicial de inserción y la actualización del estado del backend son pasos distintos. El test actual usa una espera temporal fija; eso merece investigación, pero no demuestra que la lentitud sea la causa del fallo.

### Por qué falla

La secuencia observada es: solicitar tabla → procesar mainloop durante `settle()` → evaluar `doc->in_table` → abortar la suite porque el valor es falso. No se pudo determinar si el backend terminó la operación, si el callback llegó a tiempo o si la selección/contexto del documento era el esperado.

Las ejecuciones aisladas de Calc, Impress e input pasaron. No se observaron invalid read/write ni valores sin inicializar en esas ejecuciones DOC; las asignaciones al abortar Writer no representan un cierre normal del editor.

### Bloque(s) de código que fallan

**`tests/test_edit_doc.c:30–33` — código observado:**

```c
static void settle(void) {
    test_spin(700);
}

```

**`tests/test_edit_doc.c:108–117` — código observado:**

```c
    edit_doc_command(doc, ".uno:GoToEndOfDoc", NULL);
    g_assert_cmpint(edit->combine->merge_file(edit, merge), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->page(edit), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->row(edit), ==, EDIT_ERR_INVALID);   /* not in a table */
    g_assert_cmpint(edit->insert->table(edit, 2, 3), ==, EDIT_OK);
    settle();
    g_assert_true(doc->in_table);
    g_assert_cmpint(edit->insert->row(edit), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->column(edit), ==, EDIT_OK);
    g_assert_cmpint(edit->insert->annotation(edit, "Comentario \"citado\""), ==, EDIT_OK);
```

### Evidencia (salida de la herramienta)

Extracto literal de `valgrind-doc.log`:

```text
ERROR:tests/test_edit_doc.c:114:test_writer: 'doc->in_table' should be TRUE
not ok /edit-doc/writer - ERROR:tests/test_edit_doc.c:114:test_writer: 'doc->in_table' should be TRUE
Bail out!
Unspecified Application Error
[exit 1]
```

### Dónde encontrarlo / Cómo reproducirlo

La ejecución que mostró el fallo fue la suite DOC completa, no una selección `-p` de Writer:

```bash
docker exec --user 1000:1000 studystudio-memory-audit \
  timeout 300 bash /audit/valgrind-env.sh doc-repro \
  build/audit-debug/tests/test_edit_doc
```

Para comparar, se puede correr Writer aislado con `-p /edit-doc/writer`, pero eso es una reproducción adicional propuesta; no se afirma que haya sido ejecutada en esta auditoría. El [log original](/tmp/studystudio-audit-20261106/valgrind-doc.log) identifica la semilla y el comando usado.

### Impacto

La suite no completa Writer bajo esa instrumentación y no proporciona una validación limpia del ciclo completo. No permite concluir que el usuario pierda tablas o documentos en ejecución normal. Tampoco autoriza a sumar todas las pérdidas del proceso abortado como fugas de StudyStudio.

### Sugerencia de corrección (sin aplicar)

**Propuesta de diagnóstico, sin fix causal confirmado:** registrar la llegada de callbacks y esperar un estado observable con plazo, en lugar de depender solo de 700 ms fijos. Por ejemplo, usando el helper existente:

```c
/* Proposed test diagnostic; not validated as the root-cause fix. */
static gboolean writer_table_ready(gpointer data) {
    return ((EditDOC *)data)->in_table;
}

/* After requesting the table: */
g_assert_true(test_wait(writer_table_ready, doc, 30000));
```

Si no llega el estado, inspeccionar el resultado real del documento y el callback que lo actualiza. No desactivar la aserción para obtener un PASS ni aumentar tiempos sin comprobar qué evento falta.

<a id="preparacion"></a>
# 4. Preparación y comandos de reproducción

Los comandos de las secciones P1–P7 son **instrucciones para repetir** pruebas; escribir este informe no volvió a ejecutarlas ni aplicó propuestas. Los comandos originales completos, incluidos intentos fallidos y flags expandidos, están en [COMANDOS.md](/tmp/studystudio-audit-20261106/COMANDOS.md), [commands.log](/tmp/studystudio-audit-20261106/commands.log) y al inicio de cada log.

## Entorno que se utilizó

| Componente | Versión / configuración |
|---|---|
| Imagen del proyecto | `studystudio-builder:latest`, Fedora 40 |
| Contenedor conservado | `studystudio-memory-audit`, detenido al terminar las pruebas |
| GCC | 14.2.1 |
| GTK / GLib | 4.14.5 / 2.80.5 |
| Poppler | 24.02.0 |
| LibreOffice | 24.2.7.2, Writer/Calc/Impress |
| Valgrind / GDB | 3.24.0 / 16.2 |
| Display | Xvfb `:97`, `:98`, `:99`; Cairo y GL desactivado |
| Repositorio dentro del contenedor | `/usr/src/app` |
| Artefactos dentro del contenedor | `/audit`, montado desde `/tmp/studystudio-audit-20261106` |

Las herramientas se instalaron dentro del contenedor; no se cambió el Dockerfile ni se instaló el toolchain en el host. GTK usó D-Bus y HOME temporal. Se resolvieron durante la preparación la ausencia del bus y de una entrada para el UID 1000. Los warnings del portal sobre FUSE/PipeWire no se contabilizaron como defectos del proyecto.

## Reanudar el entorno conservado

Desde `/home/crisal/StudyStudio`, para repetir pruebas:

```bash
docker start studystudio-memory-audit
docker exec -d --user 1000:1000 studystudio-memory-audit \
  Xvfb :97 -screen 0 1280x1024x24 -nolisten tcp
docker exec -d --user 1000:1000 studystudio-memory-audit \
  Xvfb :98 -screen 0 1280x1024x24 -nolisten tcp
```

Estos arranques de Xvfb corresponden a un contenedor recién reanudado: no lanzar otro servidor sobre un display ya ocupado. `/audit/gtk-env.sh` prepara el display `:97`, el bus de sesión, `GDK_BACKEND=x11`, `GSK_RENDERER=cairo`, `GDK_DEBUG=gl-disable` y `GTK_A11Y=none`. `/audit/valgrind-env.sh` usa el display `:98`, HOME de Valgrind y los flags completos de Memcheck.

Los arneses son archivos temporales de diagnóstico, no tests incorporados a `tests/`. Si `/tmp` se limpia, recuperarlos del [paquete de artefactos](/tmp/studystudio-audit-20261106/studystudio-memory-audit.tar.gz) en la misma ruta montada. El paquete contiene sus fuentes y scripts, pero no los ejecutables ni las herramientas instaladas: habrá que recompilarlos con el toolchain. Este informe conserva los conceptos, fragmentos y evidencia mínima aun sin esos archivos.

## Compilaciones utilizadas

Flags esenciales de ASan + LSan:

```text
-g -O1 -fsanitize=address,leak -fno-omit-frame-pointer
```

El enlace también incluyó `-fsanitize=address,leak`; se conservaron includes, `-DDEBUG`, `-Wall -Wextra` y las bibliotecas GTK/Poppler/GLib/archive/lzma del proyecto. Se usaron variables de línea de comandos, sin editar Makefile:

```bash
docker exec --user 1000:1000 studystudio-memory-audit bash /audit/build.sh asan
docker exec --user 1000:1000 studystudio-memory-audit bash /audit/compile-harnesses.sh asan
docker exec --user 1000:1000 studystudio-memory-audit bash /audit/build.sh debug
docker exec --user 1000:1000 studystudio-memory-audit bash /audit/compile-harnesses.sh debug
```

`build.sh asan` ejecuta `make -j2 debug tests` con `BUILD_DIR=build/audit-asan` y los flags de sanitizers tanto en objetos de debug como de tests. `build.sh debug` usa `BUILD_DIR=build/audit-debug` con `-g -O0 -fno-omit-frame-pointer`. Los logs de ambas compilaciones terminaron en 0.

Para reproducir solamente el warning P6 sin alterar objetos existentes, se puede usar un directorio nuevo con esta receta temporal:

```bash
docker exec --user 1000:1000 studystudio-memory-audit bash -lc '
  flags="-MMD -MP -Wall -Wextra -g -O0 -DDEBUG -fno-omit-frame-pointer -Iinclude -Iinclude/third_party $(pkgconf --cflags gtk4 poppler-glib gmodule-2.0)"
  libs="$(pkgconf --libs gtk4 poppler-glib gmodule-2.0) -larchive -llzma -lm"
  make debug BUILD_DIR=build/report-debug-repro "CFLAGS_LINUX_DEBUG=$flags" "LDFLAGS_LINUX=$libs"
'
```

Esta última receta es una propuesta de reproducción equivalente; no se ejecutó al redactar el informe.

<a id="cobertura"></a>
# 5. Cobertura y resultados de las ejecuciones

## ASan + LSan y tests

| Ejecución | Resultado funcional | Resultado de memoria y matiz |
|---|---|---|
| TXT existente | 5/5 pasan | Ejecución final con GL desactivado: exit 0, sin reporte LSan |
| PDF existente | 4/4 pasan | Pérdidas externas; P5 separa 6212 bytes Poppler del total inicial |
| DOC existente | 4/4 pasan | ASan activo; `_Exit` evita el chequeo automático final de LSan |
| Estrés TXT/MD/PDF | 20 ciclos por formato; 120 aperturas renderizadas pasan | Ambos órdenes de cierre ventana/editor, edición, guardado y reapertura; sin accesos inválidos, sí pérdidas externas |
| Subjects | Diez ciclos normales, TXT/MD, archivo aleatorio de 8 MiB y entradas inválidas | 59/63 comprobaciones pasan; cuatro fallos exponen P2 y P3 |
| Principal TXT/MD/TXT | Tres ciclos con selector real pasan | Guardado de bytes comprobado; residual LSan 5172 bytes / 111 asignaciones tras filtrar fontconfig |
| Principal TXT/MD/PDF/DOC | Cuatro formatos con selector real pasan | LSan explícito reporta 12781 bytes / 212 asignaciones; exit 0 es funcional, no memoria limpia |
| PDF con gesto retenido | Falla como reproducción dirigida | P1: UAF de ASan, exit 1 |
| DOC con clipboard HTML | No se pega el contenido | P4: criticals GIO/GLib y exit 2 del arnés |

Los 13 casos existentes pasaron en sus ejecuciones individuales. También se ejecutó `make check BUILD_DIR=build/audit-asan`: DOC pasó, PDF pasó sus aserciones, pero LSan hizo retornar 1 al binario PDF; Make terminó en 2 y no alcanzó TXT. **No se informa que `make check` haya pasado.**

El binario principal se ejecutó sin modificar sus fuentes, acompañado por un controlador `LD_PRELOAD` temporal que activó widgets y acciones. La selección real usó eventos X11 sobre el portal externo. En TXT se modificó GtkTextBuffer; PDF llamó al contrato público de selección/resaltado/nota; DOC emitió un commit en el GtkIMContext real. No todas las acciones fueron clicks o teclas físicas.

La comprobación posterior reabrió el PDF guardado y verificó dos notas, dos highlights y la nota nueva. También abrió el ZIP ODT y encontró el texto ingresado en `content.xml`:

```text
Saved PDF reopened: notes=2 highlights=2 expected_note=1
Saved ODT reopened: 'Audit GUI DOC edit'
[exit 0]
```

La evidencia está en [verify-main-saved-files.log](/tmp/studystudio-audit-20261106/verify-main-saved-files.log). La cobertura Office incluyó ODT/DOCX y los casos de la suite; no se certifican todas las extensiones ni un archivo binario legado `.doc`.

## Valgrind: Memcheck + LeakCheck

Los reportes completos usan:

```text
--leak-check=full --show-leak-kinds=all --track-origins=yes
--error-exitcode=1 --num-callers=30 --track-fds=yes
```

| Ejecución | Resultado funcional | Definite / indirect bytes | Accesos inválidos |
|---|---|---:|---|
| TXT | 5/5 pasan | 96 / 53 | Ninguno |
| PDF | 4/4 pasan | 1568 / 5156 | Ninguno |
| Subjects | 59/63; P2/P3 | 0 / 0 | Ninguno; tres pipes sin cerrar |
| PDF gesto retenido | Callback ejecutado tras liberar | 15200 / 55295 | Siete contextos propios, P1 |
| Estrés renderizado | Tres ciclos por formato; 18 aperturas pasan | 17536 / 66657 | Ninguno |
| DOC suite completa | Writer aborta en `:114` | 6240 / 53 | Ninguno reportado antes del aborto |
| DOC Calc aislado | Pasa, 42,40 s | 8216 / 3385 | Ninguno |
| DOC Impress aislado | Pasa, 44,81 s | 14648 / 16630 | Ninguno |
| DOC input aislado | Pasa, 74,88 s | 15600 / 31884 | Ninguno |
| Principal TXT/MD/TXT | Tres ciclos completos pasan | 40032 / 168689 | Ninguno |

Estas columnas son los totales de la herramienta, **no totales de fugas propias**. Las pérdidas normales se inspeccionaron y sus orígenes observados corresponden a Poppler, fontconfig, GIO/GTK, LibreOffice y loader. No se estableció una nueva fuga heap de ownership propio en `src/` o `include/`; sí se confirmó la fuga de descriptores P3.

El principal reportó además 14385 bytes possibly lost y 3790194 bytes still reachable; no se sumaron como fugas propias. Sus registros definite con frames del proyecto apuntan al módulo GIO inicializado por GTK, no a una asignación de StudyStudio pendiente de liberar. En Calc también apareció un `close(-1)` de CUPS, sin frame del proyecto en ese stack; no se atribuyó a EditDOC.

Se generaron suppressions con `--gen-suppressions=all` en una ejecución adicional TXT 5/5. El [archivo externo generado](/tmp/studystudio-audit-20261106/valgrind-external.supp) limita patrones a fontconfig y al módulo GIO observado; los reportes base no usaron ese archivo personalizado. Las supresiones predeterminadas instaladas de Valgrind permanecieron activas y sus conteos figuran en cada log.

## GDB línea a línea

| Sesión | Comandos `next` | Qué se recorrió |
|---|---:|---|
| Principal | 728 | `main`, activación GTK, creación de materia, callbacks de apertura, tres documentos TXT/MD, edición, guardado y destrucción |
| Subject `.xz` | 187 | Constructor, creación, guardado, carga, extracción y destructor; round trip de 10240 bytes |
| TXT/PDF render | 257 | Constructores, apertura/render y finalizadores; seis aperturas renderizadas |
| DOC enriquecido | 163 | Constructor, apertura/render, callback de pegado y finalizador; error funcional P4 |

Se registraron `info args`, `info locals`, `print` y `bt`, con breakpoints en Subject, save/load `.xz`, constructores/destructores y callbacks de GTK. Los locales impresos antes de inicializarse no se interpretaron como bugs. Las sesiones principal, Subject y render terminaron normalmente. Un backtrace separado capturó la primera señal del pegado DOC con `fatal-criticals`.

Para repetir el principal con los arneses conservados, se puede usar:

```bash
docker exec --user 1000:1000 -e AUDIT_CHOOSER_MODE=synthetic \
  studystudio-memory-audit bash /audit/gtk-env.sh \
  gdb -q -batch -ex 'set environment LD_PRELOAD=/audit/gui_driver.so' \
  -ex 'source /audit/gdb_trace.py' \
  --args build/audit-debug/studystudio-0.1.4_linux_debug
```

En GDB y Valgrind se usó selección de archivos sintética mediante GTask para mantener los callbacks reales sin depender del portal. La validación del selector visual pertenece a las ejecuciones X11 reales de ASan.

<a id="interpretacion"></a>
# 6. Cómo interpretar fugas y códigos de salida

## `_Exit` y LSan en DOC

El proyecto conserva un workaround para los destructores estáticos de LibreOffice:

**`src/main.c:644–648` — código observado:**

```c
    if (edit_doc_started()) {
        fflush(NULL);
        _Exit(status);   /* C99, <stdlib.h>: skips atexit and static destructors */
    }
    return status;
```

El test DOC hace lo mismo en `tests/test_edit_doc.c:334–336`. `_Exit` evita las rutinas normales de salida y el chequeo automático de LSan. Que ese test retorne 0 no demuestra ausencia de fugas.

Por eso se ejecutó `__lsan_do_recoverable_leak_check()` explícitamente en el arnés DOC y en el controlador del principal, después de cerrar editores. En el principal ese chequeo ocurre antes del último unref de GtkApplication, y LibreOffice global sigue alcanzable. No certifica que todos los recursos globales hayan sido destruidos.

Extracto literal de la ejecución principal de cuatro formatos:

```text
SUMMARY: AddressSanitizer: 12781 byte(s) leaked in 212 allocation(s).
[GUI] explicit LSan result=1 (nonzero: leaks reported above)
[GUI] application returned status=0 audit=PASS
[exit 0]
```

Ese 0 significa que el flujo funcional pasó; el resultado de LSan explícito fue 1. Esta limitación se registra como condición de cobertura, no como una quinta fuga propia confirmada.

## Dependencias, cachés y memoria alcanzable

La [supresión LSan utilizada](/tmp/studystudio-audit-20261106/lsan-external.supp) contiene únicamente `leak:libfontconfig.so`. No suprime Poppler ni funciones de StudyStudio. La primera inicialización X11 sin GL desactivado añadió las dos pérdidas de Mesa de 256 bytes; posteriormente se desactivó GL.

No se asume que toda asignación externa sea una caché intencional. Se diferencia entre su origen observado y una obligación de liberación del proyecto. Los reportes completos, con indirect/possible/reachable, se preservaron para examinar las dependencias.

`--error-exitcode=1` de Valgrind también falla por pérdidas de bibliotecas o descriptores. `make check` puede detenerse por ese retorno aunque las aserciones de un binario pasen. Por otra parte, `gdb -batch` puede devolver 0 al capturar una señal o al terminar una sesión: evaluar el estado del inferior y el stack, no solamente el código del proceso GDB.

<a id="limites"></a>
# 7. Limitaciones y observaciones no confirmadas

## Cargar una materia archivada no la integra en la UI

`on_load_dialog_finish()` en `src/main.c:485–497` crea un Subject temporal, carga/extrae el `.xz`, libera el buffer y cierra ese Subject. No crea su panel de edición. Por esa limitación existente, el recorrido principal crea una materia y abre sus documentos; el guardado/carga `.xz` se ejercitó y depuró por separado. No se afirma haber validado un flujo completo de editar desde la UI una materia archivada que aún no se integra.

## Riesgo estático del helper asíncrono de clipboard

En `tests/test_common.h:59–62` se pasa una variable local `TestClip` a una operación asíncrona y no se comprueba si `test_wait(..., 3000)` venció. Un callback tardío podría escribir en stack fuera de su vida. No se reprodujo durante las ejecuciones; pertenece a los tests y no entra en el conteo de defectos confirmados.

Si se investiga, cancelar no basta por sí solo: el estado debe seguir vivo hasta que el callback complete, o tener ownership independiente con referencias. No proponer liberarlo al vencer el timeout mientras la operación todavía pueda usarlo.

## Riesgo estático del bucle de compresión

En `src/subject.c:903–923` se prepara un nuevo bloque sin una comprobación explícita de input pendiente del encoder tras `lzma_code()`. Se señaló para revisar con tamaños de bloque y condiciones de I/O distintos. El round trip de 8 MiB aleatorios conservó todos los bytes; no se confirmó corrupción por esta hipótesis y no se presenta como un bug adicional.

## Alcance de plataforma y automatización

La auditoría valida Linux en las versiones de bibliotecas indicadas. No valida Windows ni todas las extensiones Office. El UAF PDF usa referencias/gestos controlados y no prueba su frecuencia con ratón real. El selector real sí se automatizó vía X11, pero otras acciones se ejercitaron mediante señales, métodos o contratos públicos.

La lentitud de Valgrind obligó a desactivar animaciones y usar un controlador con intervalos mayores para completar el principal. No se atribuye ese ajuste a un nuevo bug de rendimiento. Los intentos que se quedaron en el watchdog no se cuentan como recorridos completos.

## Intentos descartados y errores de preparación

Se conserva `valgrind-main.raw.log`, pero ese primer intento instrumentó el wrapper `env` y no el programa completo. Fue descartado. `valgrind-main-instrumented` sí instrumentó la app pero agotó el watchdog. La conclusión del principal procede de `valgrind-main-complete`, con Valgrind invocado directamente sobre StudyStudio y preload configurado antes.

También se conservaron los intentos previos al arreglo de D-Bus/usuario y los selectors ocultos que no llegaban al portal visible. Una comprobación auxiliar `nm | rg` salió en 127 porque la imagen no tenía `rg`; la compilación del controlador había terminado correctamente y el símbolo se verificó desde el host. Estos son incidentes del diagnóstico, no problemas de memoria del proyecto.

<a id="prioridades"></a>
# 8. Orden de corrección y verificación posterior

1. **P1, vida de callbacks PDF.** Desconectar antes de liberar y cubrir ambos órdenes de cierre. Exigir que el arnés de gesto retenido ya no reporte accesos inválidos, tanto con ASan como con Valgrind.
2. **P2 y P3, cleanup del guardado.** Revisarlos juntos para conservar errores, cerrar todos los recursos y recolectar al hijo. Comprobar ausencia de `Created` en `/dev/full`, FD constantes en errores repetidos y round trips normales intactos.
3. **P4, cierre del destino de pegado.** Validar que HTML/PNG se pegan, que texto plano sigue funcionando y que cerrar el documento durante la operación no provoca callbacks a datos muertos.
4. **P5, dependencia Poppler.** Evaluar versión/backport con el mínimo aislado; no esconder el UAF propio ni agregar un segundo unref al wrapper.
5. **P7 y P6.** Investigar el estado asíncrono de Writer sin relajar la aserción y aclarar el contrato del parámetro reservado.

Las propuestas de código incluidas no se aplicaron ni se ejecutaron como correcciones. La ejecución autorizada para esta etapa fue la escritura de `report.md`; no se reanudó el contenedor ni se volvieron a correr tests para redactarlo.

<a id="evidencia"></a>
# 9. Evidencia completa y referencias

Este documento contiene los fragmentos necesarios para entender cada problema. La evidencia completa de la sesión se conserva en `/tmp/studystudio-audit-20261106/`; `/tmp` es temporal y sus enlaces no sustituyen la conservación del paquete.

| Material | Ubicación |
|---|---|
| Paquete completo de informes, logs y fuentes de arneses, aproximadamente 21 MB comprimidos | [studystudio-memory-audit.tar.gz](/tmp/studystudio-audit-20261106/studystudio-memory-audit.tar.gz) |
| Registro de comandos y preparación original | [COMANDOS.md](/tmp/studystudio-audit-20261106/COMANDOS.md), [commands.log](/tmp/studystudio-audit-20261106/commands.log) |
| Informe original de ejecución | [INFORME.md](/tmp/studystudio-audit-20261106/INFORME.md) |
| Compilaciones y flags completos | [build-asan.log](/tmp/studystudio-audit-20261106/build-asan.log), [build-debug.log](/tmp/studystudio-audit-20261106/build-debug.log) |
| ASan/LSan: tests y estrés | `asan-txt-clean.log`, `asan-pdf.log`, `asan-doc.log`, `asan-check.log`, `asan-cycles.log` dentro del directorio de artefactos |
| Principal, cuatro formatos y verificación de archivos | [asan-main-all-formats.log](/tmp/studystudio-audit-20261106/asan-main-all-formats.log), [verify-main-saved-files.log](/tmp/studystudio-audit-20261106/verify-main-saved-files.log) |
| Tabla completa de Valgrind | [valgrind-summary.md](/tmp/studystudio-audit-20261106/valgrind-summary.md) |
| Valgrind del principal, recorrido válido | [valgrind-main-complete.raw.log](/tmp/studystudio-audit-20261106/valgrind-main-complete.raw.log) |
| GDB principal | [gdb-main.log](/tmp/studystudio-audit-20261106/gdb-main.log) |
| GDB Subject y editores | [gdb-subject.log](/tmp/studystudio-audit-20261106/gdb-subject.log), [gdb-pdf-render.log](/tmp/studystudio-audit-20261106/gdb-pdf-render.log), [gdb-doc-rich.log](/tmp/studystudio-audit-20261106/gdb-doc-rich.log) |
| Backtrace de la primera señal de DOC | [gdb-doc-critical-backtrace.log](/tmp/studystudio-audit-20261106/gdb-doc-critical-backtrace.log) |
| Fragmentos de fix originales, no aplicados | [recommendations.md](/tmp/studystudio-audit-20261106/recommendations.md) |
| Inventario de los artefactos originales | [INVENTARIO.json](/tmp/studystudio-audit-20261106/INVENTARIO.json) |

Los `.raw.log` de Valgrind contienen los reportes completos con todas las categorías. El chat mostró salida por bloques y truncó algunos reportes extensos; no se afirma que todas sus líneas hayan sido visibles en pantalla. Los archivos preservan la salida íntegra. `recommendations.md` contiene una referencia antigua a las líneas de `Created`; este informe la corrige a **`src/subject.c:993–994`** tras releer la fuente actual.

Referencias de API y dependencias utilizadas para interpretar el diagnóstico:

- [GIO: `g_memory_output_stream_steal_as_bytes`](https://docs.gtk.org/gio/method.MemoryOutputStream.steal_as_bytes.html).
- [Poppler: API GLib de anotaciones](https://poppler.freedesktop.org/api/glib/poppler-Poppler-Annotation.html).
- [Poppler 24.02.0: fuente oficial](https://poppler.freedesktop.org/poppler-24.02.0.tar.xz) y [Poppler 26.07.0: fuente oficial](https://poppler.freedesktop.org/poppler-26.07.0.tar.xz).

No se modificaron fuentes, headers, Makefile ni Dockerfile para generar este informe. Los cambios propuestos quedan como material de revisión para el desarrollador.

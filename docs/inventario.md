# Inventario de archivos entregados

Todos los caminos siguientes se verificaron en disco. Los archivos de evidencia y este inventario pueden actualizarse durante la comprobación final.

```text
.github/workflows/ci.yml
.gitignore
Makefile
README.md
configs/abandono.txt
configs/asedio.txt
configs/ejemplo.txt
configs/llegadas_lentas.txt
docs/defensa.md
docs/evidencia/asan-make.txt
docs/evidencia/asan.txt
docs/evidencia/lsan.txt
docs/evidencia/natural.txt
docs/evidencia/normal.txt
docs/evidencia/paquete.txt
docs/evidencia/tsan-make.txt
docs/evidencia/tsan.txt
docs/informe.pdf
docs/inventario.md
docs/pruebas.md
docs/pull_request.md
docs/requisitos.md
docs/tablero.md
include/board.h
include/config.h
include/game.h
include/log.h
include/queue.h
include/signals.h
include/tournament.h
src/board.c
src/config.c
src/game.c
src/log.c
src/main.c
src/queue.c
src/signals.c
src/tournament.c
tests/fail_pthread_create.c
tests/test_board.c
tests/test_config_queue.c
tests/test_game.c
tests/test_integration.py
tools/make_report.py
tools/package.py
```

## Productos compilados y ejecutados

- `torneo`: ocho módulos C compilados y enlazados; 12 grupos de integración.
- `build/test_board`: compilado y ejecutado.
- `build/test_config_queue`: compilado y ejecutado.
- `build/test_game`: compilado y ejecutado.
- `build/libfailpthread.so`: compilada y cargada en diez procesos de prueba.
- Versiones ASan/UBSan y TSan: compiladas y ejecutadas; ver `pruebas.md`.

Los productos de compilación existen en el repositorio local, pero `.gitignore` y el empaquetador los excluyen de la entrega de fuentes.

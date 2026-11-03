# Verificación real de la entrega

Ejecutada el 3 de noviembre de 2026 en Linux con GCC y Pthreads. Los resultados
siguientes corresponden a procesos ejecutados, no solo a inspección de fuentes.
Las salidas originales están en `docs/evidencia/`.

## Compilación y suite normal

```sh
make clean
make test
```

Resultado: salida **0**, sin advertencias. El Makefile compila los ocho módulos
`board.c`, `config.c`, `game.c`, `log.c`, `main.c`, `queue.c`, `signals.c` y
`tournament.c`, y enlaza `torneo` con `gcc -Wall -Wextra -std=c17` y `-pthread`.
Genera además tres ejecutables unitarios y una biblioteca de inyección de errores.
La ejecución final queda en `evidencia/normal.txt`.

| Prueba | Resultado observado |
| --- | --- |
| `test_board` | 7 grupos aprobados: topología, pasos/saltos, reglas de captura, seis capturas, inmovilización, formato e invariantes. Incluye 100 competencias de dos hilos por una casilla. |
| `test_config_queue` | Argumentos, enteros/overflow, claves, rangos, colisiones, carga transaccional y FIFO circular: aprobado. |
| `test_game` | Abandono de ambos bandos, mesa reutilizada, notificación tardía, cierre idempotente y actividad de 13 piezas seguida de join: aprobado. |
| Integración | 12 grupos aprobados; ningún caso omitido en la suite normal. |
| Fallos de creación | Diez ejecuciones con fallo inyectado en distintas llamadas de `pthread_create`: salida de error esperada y cierre dentro del plazo. |
| Carga y reciclaje | 100 clientes, 4 mesas, 50 partidas con tablero terminal inicial, sin duplicaciones ni saltos de FIFO. |
| Concurrencia real | Hasta cuatro mesas activas con piezas moviéndose y más parejas esperando; el pico observado respeta M. |
| SIGINT | Antes del primer emparejamiento, con mesa activa y cola, y con varias mesas: salida 130, resumen sin clientes en cola y jueces unidos. |

El test de integración compara el orden real de inserción, no el ID ni el orden
visual de líneas del log. Cada proceso tiene un timeout para que un bloqueo
observado falle la prueba. La configuración terminal se usa para verificar el
reciclaje sin depender de la duración de una partida aleatoria.

## Partida completa con movimientos reales

También se ejecutó `./torneo 2 1` con la disposición predeterminada. Terminó
con salida **0**: 677 intentos registrados, seis capturas y victoria natural del
Puma, con los trece hilos de piezas unidos. La duración observada fue 12,348 s;
es una observación de esa ejecución y no un límite ni benchmark garantizado.
Resumen conservado en `evidencia/natural.txt`.

## Sanitizadores

Se ejecutaron los objetivos públicos completos, incluidos los tres unitarios:

```sh
make asan
make tsan
make clean
make test
```

`make asan`: salida **0**, AddressSanitizer y UndefinedBehaviorSanitizer sin
reportes en las ejecuciones realizadas. LeakSanitizer se desactiva explícitamente
con `ASAN_OPTIONS=detect_leaks=0`. `make tsan`: salida **0**, ThreadSanitizer sin
reportes de carreras en las ejecuciones realizadas. En ambas suites pasan los
11 grupos de integración compatibles y se omite explícitamente el grupo de
inyección `LD_PRELOAD`; ese grupo sí se ejecuta en la compilación normal.
La evidencia más completa está en `evidencia/asan-make.txt` y
`evidencia/tsan-make.txt`; las pruebas aisladas anteriores también se conservan.

### Verificación bloqueada por el entorno

Se intentó realmente:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 work/torneo-asan 1 1
```

El programa completa el escenario, pero LeakSanitizer termina con salida **1**:
`LeakSanitizer does not work under ptrace (strace, gdb, etc)`.
La salida está en `evidencia/lsan.txt`. **No se certifica cero fugas de memoria**:
esta herramienta no pudo verificarlo en el entorno actual. Los recursos se
liberan explícitamente y los procesos probados terminan; eso no reemplaza la
verificación de fugas.

Para reproducir el análisis fuera de un depurador/trazador:

```sh
make clean
make CFLAGS='-Wall -Wextra -std=c17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined'
ASAN_OPTIONS=detect_leaks=1 ./torneo 100 4 configs/asedio.txt
ASAN_OPTIONS=detect_leaks=1 ./torneo 2 1 configs/abandono.txt
ASAN_OPTIONS=detect_leaks=1 ./torneo 12 2
# En la tercera ejecución, enviar Ctrl+C después de observar varias mesas.
```

GitHub Actions está configurado para compilar, probar y repetir un escenario con
LeakSanitizer en Ubuntu. La presencia del workflow no significa que la ejecución
remota haya pasado; su estado se consulta en el PR.

## Alcance de la evidencia

La semilla no determina el interleaving del planificador. No se midió throughput
ni latencia de movimientos. No se probaron todos los valores posibles hasta
1024 clientes/64 mesas, ni todos los fallos de malloc/inicialización de
primitivas, ni todas las planificaciones posibles. El código comprueba fallos de
inicialización y desenrolla los recursos ya creados, pero esa cobertura de fallo
no se equipara a los diez casos de creación de hilos ejecutados.

El abandono probado es cooperativo, coherente con clientes locales; no se afirma
recuperación ante corrupción arbitraria, SIGKILL o caída completa del proceso.
Tampoco se garantiza una victoria aleatoria en tiempo finito ni equidad estricta
del planificador POSIX.

El PDF final se verificó con `pdfinfo`: **2 páginas A4**. El ZIP contiene fuentes,
configuraciones, pruebas, documentación, evidencia y manifiesto SHA-256; no contiene
objetos, ejecutables, `.git` ni archivos de trabajo. Su extracción y compilación
se registran en `evidencia/paquete.txt` al completar el empaquetado.

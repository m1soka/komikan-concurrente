# Pruebas automatizadas

`make test` ejecuta los unitarios de tablero, configuración/FIFO y partidas,
además de `python3 tests/test_integration.py`. El runner de integración también
admite `--binary /ruta/torneo` y `--no-injection`.

La integración usa tableros inicialmente inmovilizados para verificar la
terminación natural y el reciclaje de mesas con 2, 10, 100 y 200 clientes. Las
partidas aleatorias se cierran mediante SIGINT; no se presupone un ganador ni
un plazo finito para el juego aleatorio. El orden FIFO se verifica contra el
número de llegada registrado bajo el mutex del torneo, no contra el ID del
cliente ni el orden de impresión de líneas concurrentes.

Los escenarios de fallo de participantes usan `fail_player`, `fail_after_ms`
y `fail_mode`. Comprueban que un `pthread_exit()` o una cancelación diferida
en un punto controlado otorgan la victoria al rival y permiten cerrar el
proceso. El unitario de partida verifica además que el mutex puede volver a
adquirirse después del fallo. Estas pruebas no simulan una corrupción de
memoria ni la recuperación de un proceso tras SIGSEGV.

`build/libfailpthread.so` se carga exclusivamente mediante `LD_PRELOAD` en
los subprocesos de prueba. Permite fallar una llamada determinada a
`pthread_create` o `pthread_mutex_init`. Sus barreras imprimen un marcador y
esperan pasivamente un byte en una FIFO para que el runner pueda enviar
SIGINT en una fase concreta:

| Variable | Operación interceptada |
| --- | --- |
| `KOMIKAN_FAIL_CREATE_AT` | La llamada indicada a `pthread_create` devuelve EAGAIN. |
| `KOMIKAN_FAIL_MUTEX_AT` | La llamada indicada a `pthread_mutex_init` devuelve EAGAIN. |
| `KOMIKAN_GATE_CREATE_AT` | Barrera antes de la llamada indicada a `pthread_create`. |
| `KOMIKAN_GATE_MUTEX_AT` | Barrera antes de la llamada indicada a `pthread_mutex_init`. |
| `KOMIKAN_GATE_FDOPEN_AT` | Barrera antes de `fdopen` durante la carga de configuración. |
| `KOMIKAN_GATE_FIFO` | Ruta de la FIFO que libera la barrera. |

La prueba de falta de memoria limita únicamente el espacio virtual del
subproceso mediante `RLIMIT_AS` y solicita una reserva representable que
excede ese límite. Las pruebas con `LD_PRELOAD` y `RLIMIT_AS` se omiten con
`--no-injection` para evitar interferir con los runtimes de sanitizadores.

Los casos de SIGINT posteriores al marcador `SERVIDOR señales_bloqueadas`
exigen salida 130. Un grupo separado envía SIGINT inmediatamente después de
`Popen` y clasifica la terminación anterior al bloqueo de señales como
`-SIGINT`; esa ventana precede a los mecanismos de la aplicación. En Linux,
los casos de cierre con SIGINT también comprueban que `/proc/PID/task` ya no
existe después de recoger el proceso. Esto demuestra la desaparición de los
hilos de ese proceso, no una auditoría global del sistema operativo.

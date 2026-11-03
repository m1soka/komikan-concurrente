# Pruebas y auditoría final

Auditoría ejecutada el **3 de noviembre de 2026**. Esta revisión conserva la
arquitectura modular y corrige los problemas descritos en `auditoria.md`.
La matriz requisito → archivo/función → prueba está en `requisitos.md`.
Los archivos `evidencia/audit-*` corresponden a esta revisión; los anteriores
`normal.txt`, `asan-make.txt`, `tsan-make.txt`, `natural.txt` y `ci.txt` se conservan
como evidencia histórica de la entrega inicial, no como prueba de código nuevo.

## Compilación y suites ejecutadas

| Comando real | Resultado | Evidencia |
| --- | --- | --- |
| `make clean && make` | Salida 0, ocho fuentes C y enlace sin advertencias, `gcc -Wall -Wextra -std=c17 -pthread` | `evidencia/audit-build.txt` |
| `make test` | Salida 0; tres unitarios y **22/22 grupos de integración**, sin omisiones | `evidencia/audit-test.txt` |
| `python3 tests/test_integration.py` | Salida 0, **22/22**; incluye barrera durante inicialización parcial de varias mesas | `evidencia/audit-integration.txt` |
| `make asan` | Salida 0; ASan y UBSan sin reportes; unitarios completos y **17 grupos aprobados / 5 omitidos** | `evidencia/audit-asan.txt` |
| `make tsan` | Salida 0; TSan sin reportes; unitarios completos y **17 grupos aprobados / 5 omitidos** | `evidencia/audit-tsan.txt` |

Las cinco omisiones de cada suite sanitizada son explícitas: inyección de
creación, memoria limitada, fallos de mutex, barreras de inicio y barrera de
configuración. Dependen de `LD_PRELOAD` o `RLIMIT_AS`, que interfieren con los
runtimes; esos mismos grupos **sí pasan en la suite normal**. La simulación de
fallos de participantes continúa activa bajo sanitizadores.

Al terminar se ejecutó nuevamente la compilación y suite normal, por lo que el
`torneo` y los productos de `build/` del repositorio local son la versión normal.
Se excluyen del ZIP mediante `.gitignore` y el empaquetador.

## Reglas, topología y actividad real

`test_board` pasa nueve grupos: compara los **784 pares de posiciones**, las
**48 aristas** del dibujo y las **70 rutas de captura dirigidas**. Prueba seis
capturas reales consecutivas, inmovilización, rechazo de saltos de Perros,
trono, formato, 100 competencias de dos hilos por una casilla y 200 posiciones
iniciales deterministas. En estas últimas ejecuta 39.127 jugadas y 634 capturas
con comprobación de invariantes. La geometría del código se conservó porque no
se encontró un error. Revisión visual y comandos adicionales en
`evidencia/auditoria-tablero.txt`.

`test_config_queue` prueba argumentos y overflow, claves/modos de fallo,
configuración transaccional, cancelación de lectura, NUL embebido, longitud de
línea, tipos de archivo y wrap de la FIFO. El parser admite archivos regulares;
rechaza una FIFO sin escritor de forma inmediata para evitar bloquear el inicio.

`test_game` verifica que el juez inicializa el tablero (`board_ready`), la
reutilización del slot, abandono de ambos bandos, avisos tardíos y salida con
actividad. Seis casos prueban fallos de piezas: ambos bandos × salida con mutex,
salida sin mutex y cancelación diferida en condición. Se comprueba ganador,
callback tras joins, invariantes y `pthread_mutex_trylock()` satisfactorio.

También se ejecutó **`./torneo 2 1`** con disposición predeterminada: salida 0,
402 intentos registrados, seis capturas, victoria natural del Puma y trece piezas
unidas. Duró 7,429 s en esa ejecución; no es un benchmark ni una garantía de
terminación aleatoria. `evidencia/audit-natural-final.txt` conserva el resumen y la
comprobación posterior de `/proc/PID/task`.

## Capacidad, FIFO, fallos y SIGINT

La integración comprueba, entre otros casos:

- **2, 10, 100 y 200 clientes**, con 1, 3, 4 y 8 mesas respectivamente, usando un
  tablero ganador para terminar sin depender del azar. Comprueba parejas FIFO,
  cantidad de resultados, ausencia de duplicaciones, pico ≤ M y reutilización.
- Partidas con 200 clientes y cuatro mesas realmente activas, más clientes
  esperando; SIGINT termina con 130 y vacía la cola.
- `M=65` y `M=SIZE_MAX` válidos para `N=2`; prueba de `N=1025` que supera el
  análisis de argumentos y reservas antes de un fallo inyectado del emparejador.
  Esto demuestra eliminación de los topes, no una carga exitosa de 1025 clientes.
- Rechazo de `SIZE_MAX+1`, tamaños no representables y falta de memoria real
  mediante `RLIMIT_AS=128 MiB` en el subproceso. No se reduce el límite de memoria
  del proceso que ejecuta las pruebas.
- Diez fallos de `pthread_create`, tres fallos de `pthread_mutex_init` y cinco
  modos de salida inesperada, distinguidos del abandono cooperativo.
- Barreras pasivas de prueba antes de crear el receptor de señales, emparejador,
  primer cliente y piezas; durante inicialización parcial de varias mesas
  (`N=6, M=3`), y durante `fdopen` de configuración. Se envía SIGINT y se libera
  la barrera; todos estos casos terminan con **130**.
- Diez envíos después del marcador `SERVIDOR señales_bloqueadas`, todos con 130.
  Otros diez envíos inmediatamente después de `Popen` terminaron **antes** de
  ejecutar el bloqueo, con `-SIGINT`. Se clasifican como ventana previa al
  mecanismo de la aplicación, no como cierres ordenados.

Los procesos tienen plazos de observación. En Linux, las pruebas de cierre
comprueban que `/proc/PID/task` ya no existe después de recoger el proceso.
Eso acredita que no quedan hilos de esos procesos; no equivale a examinar todos
los recursos del sistema operativo. No se crean semáforos con nombre ni IPC
System V: los recursos del programa son privados del proceso.

## LeakSanitizer y GitHub Actions

El intento local actualizado fue:

```sh
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 work/torneo-audit-lsan 2 1 configs/fallo_cliente.txt
```

El escenario termina y adjudica al rival, pero LSan sale con **1** y el mensaje
`LeakSanitizer does not work under ptrace (strace, gdb, etc)`.
La compilación y salida exactas están en `evidencia/audit-lsan.txt`. **No es un
reporte de fuga**, y tampoco una verificación satisfactoria de ausencia de fugas.

El workflow actualizado ejecuta `make test`, ASan/UBSan y, fuera del trazador
local, `ASAN_OPTIONS=detect_leaks=1 make unit` más la integración compatible con
LSan activo. Publica `komikan-concurrente.zip` como artefacto. El estado de la
revisión exacta se comprueba en el [PR #1](https://github.com/m1soka/komikan-concurrente/pull/1);
la mera presencia del YAML no constituye evidencia de éxito. La consulta y
resultado de CI de la revisión se incorporan en `evidencia/audit-ci.txt` cuando
se completa la ejecución, identificando el commit que se probó.

Para reproducir fuera de un depurador/trazador:

```sh
make clean
make CFLAGS='-Wall -Wextra -std=c17 -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined' all unit
ASAN_OPTIONS=detect_leaks=1 python3 tests/test_integration.py --no-injection
```

## Alcance y entrega

Las pruebas no demuestran ausencia de carreras, fugas o starvation en todos los
interleavings. No se promete recuperación de SIGSEGV, SIGKILL, cancelación
asíncrona, corrupción de memoria/mutex ni errores de join causados por handles
inválidos. Los fallos soportados ocurren en estados consistentes con cleanup
registrado. El código contempla un cliente que termina antes del registro;
esa ruta tiene revisión de bookkeeping, pero no se le atribuye una inyección
externa específica. No se probaron todos los valores representables de N/M ni
todos los fallos de cada función POSIX. No hay garantía de victoria aleatoria en
tiempo finito ni de salida acotada si el sistema bloquea E/S de forma no
interrumpible.

El informe se regenera y comprueba con `pdfinfo`: máximo **dos páginas A4**.
El ZIP se genera desde los archivos reales, excluye `.git`, objetos, ejecutables
y temporales, e incluye `MANIFEST.sha256`. La extracción, hashes y compilación
se registran en `evidencia/audit-paquete.txt`. El destino local de descargas
`/codex/browser/.../output` no permite creación en este ejecutor; la entrega
se ofrece mediante el artefacto de GitHub Actions del repositorio privado.

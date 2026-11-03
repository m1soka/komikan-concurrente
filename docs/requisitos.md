# Requisitos y trazabilidad

## Fuentes y alcance

La fuente de reglas y requisitos académicos es `T2_os_2026_02 (2).pdf`, cuatro
páginas. El texto adjunto describe preferencias de implementación y entregables;
no convierte decisiones adicionales en requisitos expresos del PDF. La petición
directa exige crear los archivos reales, compilar, probar, documentar resultados,
generar el ZIP y preparar un Pull Request. La petición de auditoría posterior
exige corregir fallos reales, actualizar el PR #1 y verificar sus comprobaciones
sin fusionarlo. Esas solicitudes adicionales se distinguen aquí de las reglas
académicas del PDF.

La solución utiliza clientes simulados mediante hilos dentro de un solo proceso.
El PDF exige llegadas concurrentes pero no prescribe sockets ni un protocolo de
red. Cada cliente representa un bando de una partida; las piezas son hilos
adicionales, no clientes adicionales.

## Matriz de trazabilidad

La columna «estado» describe la implementación y el alcance de su prueba, no una
garantía universal. Los resultados y omisiones de cada ejecución están en
`pruebas.md` y `evidencia/`. Las referencias «integración NN» identifican el método
`test_NN_...` de `tests/test_integration.py`.

| Fuente / requisito | Archivo y función | Prueba concreta | Estado / alcance |
| --- | --- | --- | --- |
| PDF §2.1: 5×5 y trono de 28 posiciones | `src/board.c:board_adjacent()`, `coordinates()` | `test_board.c:test_topology()`, `test_throne()` | Implementado; las 784 relaciones se contrastan con una transcripción independiente del dibujo. |
| PDF §2.2: un Puma y doce Perros | `include/board.h:PIECE_COUNT/DOG_COUNT`, `board_init()`, `src/game.c:game_run()` | `test_initialization_and_validation()` y `test_game.c` | Implementado; tablero inicialmente terminal no necesita crear hilos de piezas. |
| PDF §2.2: paso y captura legal | `board_moves()`, `straight_jump()`, `captured_piece()`, `board_apply()` | `test_moves_and_capture()`, `test_all_capture_routes()`, `test_throne()` | Implementado; 70 rutas dirigidas, con interpretación explícita del salto central del trono. |
| PDF §2.2: Perros sin captura | `board_moves()`, `board_apply()` | `test_moves_and_capture()` | Implementado; se rechazan captura y salto de Perro. |
| PDF §2.2: seis capturas del Puma | `board_result()`, `src/game.c:check_victory_locked()` | `test_victory()`; evidencia de partida natural en `pruebas.md` | Implementado; prueba unitaria del umbral y evidencia de una ejecución completa. |
| PDF §2.2: inmovilización del Puma | `board_result()`, `board_moves()` | `test_victory()`; integración 04 | Implementado; se consideran pasos y saltos como salidas válidas. |
| PDF §2.3: competencia sin turnos | `src/game.c:piece_run()`, `piece_body()`, `oldest_intent()`, `game_run()` | `test_game.c:wait_for_activity()`; integración 06/12/18 | Implementado; hilos independientes publican propuestas y el juez las serializa por llegada. |
| PDF §2.3: una pieza por nodo y captura atómica | `src/game.c:game_run()` protege `board_apply()` y `board_valid()` | `test_contested_cell()`; `test_legal_play_invariants()`; sanitizadores | Implementado; la prueba de contención ejercita el contrato del mutex directamente, no todos los interleavings del juez. |
| PDF §2.3: retardos de 50 a 300 ms | `src/config.c:config_load_interruptible()`, `src/game.c:piece_body()`, `after_ms()` | `test_config_queue.c:test_config()`; integración 02; revisión del cálculo | Implementado; no se presenta un benchmark ni una distribución temporal medida. |
| PDF §3: `./torneo N M [config.txt]` | `src/main.c:main()`, `parse_positive()` | `test_numbers()`; integración 01/13/14 | Implementado; sin topes 1024/64, sujeto a representabilidad y recursos. |
| PDF §3.1: configuración, semilla y disposición | `config_defaults()`, `config_load_interruptible()`, `parse_dogs()`, `read_line()` | `test_config()`, `test_failure_modes()`, `test_interruptible_config()`; integración 02/21/22 | Implementado; parser estricto y transaccional, solo archivos regulares. |
| PDF §3.2: N clientes y parejas por llegada | `src/tournament.c:tournament_run()`, `client_run()`, `matchmaker_run()` | Integración `assert_fifo()` y grupos 04/05/10/18 | Implementado; pruebas con distintas cargas, sin prometer N ilimitado. |
| FIFO solicitado | `src/queue.c:queue_push()/queue_pop()`, `matchmaker_run()` | `test_queue()`; integración `assert_fifo()` | Implementado; orden de registro efectivo, no orden de IDs o impresión. |
| PDF §3.2: máximo M mesas | `matchmaker_run()`, `on_game_finished()`; estados del slot | Integración 05/06/10/11/12/18 | Implementado; cada prueba comprueba ocupación y/o pico frente a M. |
| PDF §3.2: espera pasiva | `client_wait()`, `matchmaker_run()`, `piece_body()` | Revisión de `pthread_cond_wait/timedwait`; integración 06/11 | Implementado; los resultados de caja negra por sí solos no demuestran pasividad. |
| PDF §3.3: juez independiente | `matchmaker_run()` crea `src/game.c:game_run()` | Integración 06/12 y revisión de `pthread_create()` | Implementado; un juez por partida activa. |
| PDF §3.3.1: inicialización del tablero por el juez | `src/game.c:game_run()` invoca `board_init()` y publica `board_ready` | `test_game.c:main()` y `test_unexpected_piece_exits()` | Corregido en auditoría; `game_prepare()` solo valida disposición y prepara metadatos. |
| PDF §3.3.1: sincronización preparada | `game_init()`, `game_prepare()`, `game_run()`, `game_destroy()` | `test_game.c` reutilización; integración 15/16 | Implementación con responsabilidades compartidas: primitivas persistentes del slot y estado de partida bajo el juez. No se atribuye al juez la llamada externa a `pthread_mutex_init()`. |
| PDF §3.3.2: validación por el juez | `game_run()`→`board_apply()` dentro del mutex | `test_moves_and_capture()`, `test_all_capture_routes()`; revisión de `game_run()` | Implementado; se revalida contra el tablero actual, aunque la pieza calculó un destino antes. |
| PDF §3.3.3: tablero y eventos visibles | `game_run()`, `board_format()`, `src/log.c:log_event()` | `test_format()`; integración 06/12 y logs | Implementado; snapshots consistentes y salida fuera del mutex del tablero. |
| PDF §3.3.4: victoria, final y reutilización | `check_victory_locked()`, `game_run()`, `on_game_finished()`, `matchmaker_run()` | `test_victory()`, `test_game.c`; integración 04/05/10 | Implementado; se unen piezas y juez antes de reutilizar el slot. |
| PDF §4.1: abandono cooperativo | `client_body()`, `game_abandon()`, `finish_locked()` | Integración 08; `test_game.c` ambos bandos y aviso tardío | Implementado; victoria del rival y cierre normal del torneo. |
| PDF §4.1: salida inesperada de cliente | `client_cleanup()`, `game_participant_failed()`, `check_failures_locked()` | Integración 20, modos `client_exit_locked/unlocked`; `test_game.c` fallo anterior a inicialización | Corregido para `pthread_exit()` en estados consistentes; la ruta previa al registro tiene bookkeeping y revisión, sin atribuirle una prueba externa no ejecutada. |
| PDF §4.1: salida inesperada de pieza | `piece_cleanup()`, `piece_lock()/piece_unlock()`, `check_failures_locked()` | `test_unexpected_piece_exits()` seis casos; integración 20 | Corregido para salida dentro/fuera del mutex y cancelación diferida en espera; sin desbloqueo por hilo ajeno. |
| Fallos de infraestructura | `tournament_run()`, `game_init()`, `game_run()` | Integración 09/14/15 | Implementado con limpieza parcial; pruebas de creación, memoria e inicialización seleccionadas, no todos los fallos posibles. |
| PDF §4.2: SIGINT durante inicio | `main()`→`signals_block()/signals_start()`, `signals_interrupted()`, `signals_bind()`, parser interrumpible | Integración 16/17/19/22 | Corregido; se distingue señal previa al bloqueo de cierre ya controlado. |
| PDF §4.2: SIGINT con llegadas, mesas y cola | `signal_worker()`→`on_interrupt()`→`request_stop()`→`game_stop()` | Integración 06/07/11/12/18 | Implementado; notificación pasiva y unión de hilos. |
| PDF §4.2: liberar memoria y recursos | `game_run()` une piezas; matcher une jueces; `tournament_run()` une clientes y libera; `signals_finish()` une señales | Suites, sanitizadores y escenarios de fugas identificados en `pruebas.md` | Cobertura dinámica limitada a escenarios ejecutados; no se certifican cero fugas para toda ejecución. |
| PDF §5: informe ≤2 páginas y tres temas | `tools/make_report.py`→`docs/informe.pdf` | Revisión de contenido y `pdfinfo` en evidencia final | Entregable verificable; debe regenerarse después de modificar diseño. |
| PDF §6: GCC C17 y primitivas POSIX | `Makefile`, módulos C | `make clean && make`, `make test`, sanitizadores y CI identificados en evidencia | Implementado; los resultados se atribuyen al commit probado. |
| Petición: entrega real y PR #1 | `tools/package.py`, `.gitignore`, documentación y Git | Inventario, extracción/compilación del ZIP y consulta de Actions | Verificación en `pruebas.md`; no equivale a fusionar el PR. |

## Decisiones para aspectos no definidos

1. **Topología.** Se modelan 28 círculos: 25 de la cuadrícula, dos laterales
   del trono y su vértice. El cruce de líneas central sin círculo no añade casilla.
   Solo las líneas del diagrama representan aristas. La frase «cualquier
   dirección» permite seguir cualquier arista, no inventa diagonales.
2. **Saltos.** Se exige una secuencia de dos aristas colineales, en el mismo
   sentido, sobre un Perro y hasta un nodo libre. La continuidad del grafo prima
   sobre la longitud gráfica de segmentos del trono. No se salta girando una
   esquina ni sobre una casilla vacía.
3. **Posición inicial.** El PDF permite configurarla y no establece una única
   disposición predeterminada. La configuración por defecto es una elección de
   esta implementación; puede reemplazarse conservando un Puma y doce Perros sin
   solapamientos.
4. **Cantidad impar.** Se acepta `N` impar y se notifica al último cliente sin
   rival una vez agotadas las llegadas. No se lo deja esperando indefinidamente.
5. **Capacidad.** Se retiraron los topes 1024/64, ausentes del PDF. N y M deben
   caber en `size_t`; se validan productos por `sizeof`, multiplicaciones de
   retardos y plazos representables en `time_t`. Solo se reservan
   `min(M, N/2)` slots. `calloc()` y `pthread_create()` pueden fallar por recursos
   reales; se limpian los recursos inicializados. Validar aritmética no implica
   prometer una reserva físicamente viable.
6. **FIFO.** Se utiliza el orden de inserción efectiva de clientes; el planificador
   puede hacer que un ID posterior se registre antes que uno anterior. El FIFO no
   garantiza la planificación justa de cada hilo ni de cada ficha.
7. **Abandono y salida inesperada.** Se distinguen abandono notificado,
   `pthread_exit()` de cliente/pieza en estados consistentes, cancelación diferida
   de pieza en condición y fallo al crear infraestructura. Los primeros casos
   notifican al juez para victoria rival; el último termina con error. Los
   cleanup handlers registran propiedad y liberan solo mutexes propios. Los
   clientes tienen cancelación deshabilitada. `fail_after_ms` comienza cuando el
   cliente observa su asignación; no es un plazo desde el primer mensaje del juez.
   No se recuperan corrupción, señales fatales, cancelación asíncrona o muerte del
   proceso completo.
8. **Ausencia de empate.** No se agrega una victoria artificial por límite de
   movimientos o duración. Una partida aleatoria puede durar indefinidamente;
   `SIGINT` proporciona una terminación administrativa ordenada.
9. **Semilla.** Cada hilo tiene estado pseudoaleatorio local. La semilla controla
   sus números, pero la planificación concurrente mantiene no determinista la
   secuencia global de movimientos.
10. **Primitivas.** Pthreads y memoria compartida del propio proceso satisfacen
    la alternativa POSIX del PDF. No se crean semáforos con nombre ni segmentos
    System V: solo deben liberarse los recursos realmente usados.
11. **Inicialización y reutilización.** `game_init()` crea mutex y condición una
    vez por slot al iniciar el servidor. Tras hacer `join` al juez anterior,
    `game_prepare()` valida disposición y prepara configuración/identidades;
    deja `board_ready=false`. El juez llama a `board_init()` bajo el mutex y
    publica el tablero listo antes de crear piezas. Las primitivas persistentes
    permiten comunicar interrupciones y fallos aun antes de iniciar ese juez; se
    destruyen al cerrar el servidor, después de recoger todos sus usuarios. La
    inicialización de esas primitivas es responsabilidad del servidor, mientras
    el juez usa y coordina la sincronización de la partida.
12. **Orden de propuestas.** Cada pieza publica como máximo una propuesta
    pendiente, con secuencia protegida por el mutex del tablero. El juez procesa
    la de menor secuencia y revalida el destino. No se impone un turno de pieza:
    el orden nace de la concurrencia de las propuestas.
13. **Inicio y señales.** `signals_block()` es la primera operación de `main()`.
    `signals_start()` precede al parser y las reservas; su bandera atómica no
    depende de que el contexto del torneo ya exista. `signals_bind()` serializa
    callbacks y aplica una interrupción pendiente. Desvincular antes de destruir
    ese contexto evita callbacks sobre memoria liberada. No se garantiza cierre
    de aplicación para una señal llegada antes de ejecutar su bloqueo inicial.
14. **Archivo regular.** El parser usa apertura no bloqueante y verifica el tipo
    antes de leer; rechaza FIFO, directorios, NUL y líneas demasiado largas. Esto
    evita que una ruta de configuración no regular bloquee el inicio. Se consulta
    la bandera de interrupción mientras se leen caracteres.

## Límites de la evidencia

Una prueba funcional con timeout puede detectar un bloqueo observado, pero no
demuestra ausencia de todos los interleavings problemáticos. AddressSanitizer y
ThreadSanitizer aportan evidencia en las ejecuciones que el entorno permita;
ninguno sustituye los invariantes ni una revisión del ciclo de vida. El informe
de pruebas debe distinguir ejecución satisfactoria, fallo del programa y fallo de
la herramienta o de su runtime. Los escenarios con wrappers `LD_PRELOAD` y
agotamiento intencional de memoria se omiten en la integración sanitizada; se
conservan en la suite normal. LeakSanitizer informó una limitación local por
`ptrace`; los resultados remotos solo verifican sus escenarios y commits
identificados. Las cuentas finales, omisiones y resultados están en `pruebas.md`.
La detección de salida anterior al registro de un cliente tiene una ruta de
limpieza, pero no se presenta como probada por el test configurado, que actúa
después de asignar una mesa.

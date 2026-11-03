# Auditoría técnica y correcciones

Se contrastaron el PDF oficial de cuatro páginas, la figura del tablero de su
página 2, los ocho módulos C, los contratos de sus encabezados y las pruebas.
La petición posterior de auditoría agrega escenarios de fallo, capacidad y
señales tempranas; no cambia las reglas del juego. La matriz completa
requisito → archivo/función → prueba → alcance está en `requisitos.md`.
Los comandos, resultados y commits efectivamente probados se conservan en
`pruebas.md` y `evidencia/`. Una prueba existente o un resultado del PR anterior
no se presenta como validación automática del código corregido.

## Problemas reales corregidos

### 1. La salida inesperada no tenía limpieza de participante

La versión inicial soportaba abandono cooperativo y fallos de creación, pero no
tenía cleanup handlers en clientes ni piezas. Un `pthread_exit()` durante una
espera o una cancelación diferida de pieza podía dejar un mutex retenido o una
propuesta sin trabajador. Si un cliente salía antes de registrarse, la condición
que esperaba todas las llegadas podía quedar inalcanzable.

`src/tournament.c:client_run()` ahora instala `client_cleanup()` y deshabilita la
cancelación de clientes. El wrapper comprueba bajo mutex que un retorno normal
solo se marque cuando el cliente terminó o el torneo está cerrando. El cleanup
distingue retorno normal y salida inesperada,
registra si su hilo posee el mutex y lo libera únicamente desde su propietario.
Retira al cliente no asignado sin alterar el orden relativo de los demás, cuenta
salidas anteriores al registro y notifica `game_participant_failed()` cuando
había una partida asignada. El juez consume el evento en
`src/game.c:check_failures_locked()` y declara ganador al rival con motivo
`fallo_cliente` si la partida continúa en curso.

`src/game.c:piece_run()` instala `piece_cleanup()`. El wrapper también ejecuta
limpieza al retornar el cuerpo, distinguiendo ficha capturada, partida terminada
y salida prematura. `piece_lock()/piece_unlock()` deshabilitan cancelación durante
las transiciones de propiedad. Una cancelación diferida en `pthread_cond_wait()`
ejecuta el cleanup después de recuperar el mutex, conforme a POSIX. El handler
retira la propuesta pendiente, marca `piece_failed` y despierta al juez, que
adjudica el rival con motivo `fallo_pieza`. Las piezas no modifican el tablero;
los fallos soportados no interrumpen una escritura parcial del juez.

`src/log.c:log_event()` también deshabilita cancelación mientras posee el mutex
de salida, porque escribir y vaciar un stream puede alcanzar puntos de
cancelación. Un cleanup propio libera ese mutex ante `pthread_exit()` y el estado
de cancelación se restaura después de liberarlo. El cierre habitual sigue
siendo cooperativo; la cancelación de prueba es diferida y afecta a la propia
pieza en una espera, nunca es asíncrona.

Pruebas concretas: `tests/test_game.c:test_unexpected_piece_exits()` cubre ambos
bandos y tres modalidades de salida de pieza, comprueba victoria rival, tablero
válido, mutex disponible mediante `trylock` y reutilización. Su `main()` comprueba
además una notificación de fallo de cliente antes de inicializar el tablero y
rechaza avisos tardíos de parejas anteriores. Integración 20 ejecuta los cinco
modos no cooperativos desde el proceso completo. La ruta de cliente anterior a
registrarse tiene contabilidad y revisión, pero estos modos configurables actúan
tras la asignación: no se les atribuye una prueba externa de aquella ruta.

### 2. El tablero se inicializaba en el emparejador

En la versión inicial, `game_prepare()` llamaba a `board_init()` desde el
emparejador. La validación de jugadas sí estaba en el juez, pero la inicialización
del tablero difería de la responsabilidad indicada por PDF §3.3.1.

Ahora `game_prepare()` valida la disposición y prepara configuración e identidades
con `board_ready=false`. `game_run()`, ejecutada por el juez independiente,
inicializa el tablero compartido bajo su mutex, publica `board_ready`, consume
fallos pendientes, comprueba victoria inicial y crea las piezas necesarias. El
estado terminal inicial permite finalizar sin crear piezas inútiles.

Las llamadas a `pthread_mutex_init()` y `pthread_cond_init()` siguen en
`game_init()`, ejecutada por el servidor una vez por slot. Son infraestructura
persistente que permite comunicar cierre o fallo aun antes de iniciar el juez.
El juez coordina esas primitivas durante su partida; el emparejador hace `join`
al juez anterior antes de preparar otra pareja y el servidor destruye las
primitivas cuando ya no existen usuarios. No se afirma que cada juez cree de
nuevo el mutex: la preparación de sincronización está distribuida de esta forma
explícita para evitar doble inicialización y destrucción prematura.

Pruebas concretas: `test_game.c` comprueba `board_ready=false` tras preparación y
un tablero válido después de ejecutar al juez, reutilización y un fallo recibido
antes de esa inicialización. Integración 04/05/10 verifica resultados y reciclaje.

### 3. Los topes 1024 clientes y 64 mesas eran arbitrarios

El PDF no establece esas cifras y la versión inicial las rechazaba en la CLI.
Se retiraron. `src/main.c:main()` analiza enteros hasta `SIZE_MAX`; no convierte
una cadena desbordada en un valor válido. `tournament_run()` comprueba productos
de asignación por `sizeof`, capacidad de la cola y multiplicación de retardos.
`deadline_ms()` comprueba la conversión y suma de segundos contra `time_t`.
`queue_init()` ya rechazaba capacidades superiores a `SIZE_MAX / sizeof(size_t)`.
En este entorno POSIX, esa cota también mantiene segura la suma `head + count`;
se conservó el código de la FIFO porque no se encontró un overflow alcanzable
mediante su API de inicialización.

Se reservan `min(M, N/2)` slots. Los fallos reales de memoria o creación de hilos
se atienden mediante limpieza parcial y salida de error. Esto elimina una
restricción innecesaria, pero no promete cantidades ilimitadas ni que toda
reserva representable sea físicamente posible.

Pruebas concretas: `test_config_queue.c:test_numbers()/test_queue()` cubren
límites aritméticos; integración 13 acepta M=65 y M=`SIZE_MAX` con solo dos
clientes, integración 14 provoca agotamiento de espacio virtual y los escenarios
de carga comprueban clientes y mesas realmente creados. Aceptar un M grande con
una sola pareja no demuestra que sea viable activar tantas mesas.

### 4. SIGINT se bloqueaba después de una parte del inicio

Antes, `signals_start()` bloqueaba SIGINT cuando ya se habían leído argumentos y
configuración y reservado recursos del torneo. Era una ventana controlable en la
que se conservaba la terminación predeterminada del sistema.

Ahora `signals_block()` es la primera operación de `main()` y `signals_start()`
precede al parser y a las reservas. El hilo con `sigwait()` mantiene una bandera
atómica de interrupción. `signals_bind()` vincula el callback cuando el contexto
del torneo está listo y aplica una interrupción ya observada; desvincular espera
los callbacks en curso antes de destruir el contexto. Durante la inicialización
parcial se revisa esa bandera y se liberan solo los recursos ya inicializados.

El parser añade `config_load_interruptible()`, revisa interrupción al leer y
rechaza FIFO/directorios mediante apertura no bloqueante y validación de archivo
regular. Esto evita una espera indefinida en una ruta de configuración no regular.
También rechaza NUL incrustado, que antes podía ocultar bytes detrás del final de
una cadena C. La carga es transaccional: un error no instala una configuración
parcial.

Pruebas concretas: integración 16 usa barreras deterministas en creación e
inicialización; 17 repite señales tras el marcador de bloqueo; 19 clasifica la
ventana anterior al bloqueo; 22 interrumpe la carga de configuración. Integración
06/07/11/12/18 cubre llegadas, mesas ocupadas, cola y partidas con hasta 200
clientes. `test_interruptible_config()` comprueba el aborto del lector y
conservación de configuración; integración 21 rechaza FIFO y directorios.

## Responsabilidades del juez comprobadas en fuentes

| Responsabilidad del PDF | Ruta concreta | Observación de auditoría |
| --- | --- | --- |
| Inicializar tablero | `game_run()`→`board_init()` bajo `game->mutex` | Corregido: la llamada que escribe el tablero compartido está en el hilo juez. |
| Preparar sincronización | `game_init()` crea primitivas persistentes; `game_prepare()` reinicia metadatos; `game_run()` publica tablero y notifica | Responsabilidad distribuida explícita; no hay doble inicialización por partida. |
| Validar movimientos | `game_run()` consume intent y llama a `board_apply()` bajo mutex | Ya existía: la propuesta de pieza se revalida contra estado actual. |
| Registrar eventos y matriz | `game_run()`→`board_format()` y después `log_event()` fuera del mutex de tablero | Ya existía; se protege además la salida frente a cancelación diferida. |
| Detectar victoria/anomalía | `check_victory_locked()`, `check_failures_locked()`, `finish_locked()` | Conserva primer resultado terminal y agrega salida inesperada soportada. |
| Finalizar y liberar mesa | Juez une piezas y llama `on_game_finished()`; matcher une juez antes de `SLOT_FREE` | Se libera disponibilidad después de recoger usuarios del tablero. |

## Problemas descartados al revisar

- **Movimientos aplicados sin árbitro.** Las piezas calculan propuestas; el juez
  las aplica con `board_apply()` y vuelve a comprobar reglas y ocupación. No se
  encontró esa omisión.
- **Tablero tratado como rectángulo uniforme.** El grafo ya incorporaba el trono:
  40 aristas de cuadrícula y ocho adicionales. La auditoría amplía la prueba a
  todos los pares de nodos y rutas de captura; no añade conexiones que el dibujo
  no contiene.
- **Overflow adicional en la FIFO.** Su guarda de reserva ya acota la suma
  de índices para las capacidades válidas en este entorno; se descartó como
  problema real y se preservó el módulo.
- **Exceso de mesas o espera activa.** La reserva se hace sobre slots libres bajo
  el mutex del torneo; la espera usa condiciones con predicados. Las pruebas
  comprueban el límite observado, mientras la pasividad se revisa también en el
  código.
- **Una partida larga implica deadlock.** El movimiento aleatorio puede ciclar
  legalmente. El PDF no define empate ni plazo y no se agrega una victoria
  artificial. La finalización aleatoria observada se distingue de la cancelación
  administrativa por SIGINT.
- **Toda señal inmediata debe producir salida 130.** No se puede garantizar
  comportamiento de aplicación antes de que ejecute su primera instrucción de
  bloqueo. Tras el marcador `SERVIDOR señales_bloqueadas`, las pruebas exigen el
  cierre controlado; antes de él distinguen la terminación por la señal del sistema.

## Límites que siguen explícitos

El soporte de salida inesperada cubre los estados consistentes del modelo y los
puntos de cancelación diferida indicados. No recupera corrupción arbitraria,
SIGSEGV, SIGKILL, cancelación asíncrona, una escritura del juez interrumpida a
mitad o muerte del proceso completo. No se desbloquea un mutex ajeno para aparentar
recuperación.

POSIX no garantiza equidad estricta del planificador o mutexes. FIFO evita
adelantamientos en admisión y secuencias ordenan propuestas pendientes, pero no
garantizan un plazo máximo por ficha. Tampoco se promete victoria aleatoria en
tiempo finito. Los retardos y las pruebas con timeout no sustituyen esas garantías.

La limpieza se verifica por rutas revisadas y escenarios ejecutados. ASan/UBSan,
TSan y comprobaciones de fugas aportan evidencia solo donde su runtime funciona;
la limitación local de LeakSanitizer bajo `ptrace` permanece documentada. No se
certifican todas las planificaciones ni todos los fallos posibles de cada API.
Los wrappers y barreras de prueba se omiten de la integración sanitizada cuando
interfieren con sus runtimes y se ejecutan por separado en la suite normal.

El PR #1 se actualiza para revisión, sin fusión automática. Su commit, estado de
GitHub Actions y artefacto ZIP se identifican en la evidencia final; resultados
históricos no se atribuyen a commits posteriores.

## Resultados locales de la auditoría

Los siguientes resultados se contrastaron con los archivos reales de evidencia:

| Ejecución | Resultado | Evidencia |
| --- | --- | --- |
| `make clean && make` | Salida 0, GCC C17 con `-Wall -Wextra`, sin advertencias. | `evidencia/audit-build.txt` |
| `make test` | Tres unitarios y 22 grupos de integración aprobados; salida 0. | `evidencia/audit-test.txt` |
| Integración independiente | 22 grupos aprobados; salida 0. | `evidencia/audit-integration.txt` |
| `make asan` | Unitarios aprobados y 17 grupos de integración aprobados, cinco omitidos explícitamente; sin reportes de ASan/UBSan y salida 0. | `evidencia/audit-asan.txt` |
| `make tsan` | Unitarios aprobados y 17 grupos de integración aprobados, cinco omitidos explícitamente; sin reportes de carreras y salida 0. | `evidencia/audit-tsan.txt` |
| Partida normal `./torneo 2 1` | Salida 0, seis capturas del Puma, 402 intentos y 13 piezas unidas; 7,429 s observados. | `evidencia/audit-natural-final.txt` |
| LeakSanitizer con fallo de cliente | El escenario termina y adjudica al rival; el runtime de LSan falla después por `ptrace`, salida 1. No valida fugas localmente. | `evidencia/audit-lsan.txt` |
| Auditoría del grafo | 784 relaciones, 48 aristas no dirigidas, 70 capturas y 200 disposiciones; pruebas normales, ASan/UBSan y TSan aprobadas. | `evidencia/auditoria-tablero.txt` |

Las cinco omisiones de integración sanitizada son los grupos 09, 14, 15, 16 y
22: wrappers de creación/inicialización, agotamiento de memoria y barreras de
inicio/configuración. Sí se ejecutan en la suite normal. La partida natural es
una observación concreta, no una cota de duración. La evidencia de esa ejecución
comprueba también que su ruta `/proc/PID/task` ya no existe; no se extrapola a
todas las ejecuciones posibles.

El ensayo de SIGINT inmediatamente tras lanzar clasificó diez señales antes del
bloqueo y ninguna como cierre controlado en esa tanda. Las pruebas que esperan
el marcador de bloqueo y las barreras deterministas son las que comprueban el
cierre dentro del mecanismo de la aplicación. Se conservan ambos resultados sin
presentar la ventana previa como un fallo recuperable por el programa.

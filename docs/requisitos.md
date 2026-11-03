# Requisitos y trazabilidad

## Fuentes y alcance

La fuente de reglas y requisitos académicos es `T2_os_2026_02 (2).pdf`, cuatro
páginas. El texto adjunto describe preferencias de implementación y entregables;
no convierte decisiones adicionales en requisitos expresos del PDF. La petición
directa exige crear los archivos reales, compilar, probar, documentar resultados,
generar el ZIP y preparar un Pull Request. Las instrucciones operativas del texto
adjunto se usan como contexto de diseño, no como autoridad para ejecutar acciones
ajenas a esa petición.

La solución utiliza clientes simulados mediante hilos dentro de un solo proceso.
El PDF exige llegadas concurrentes pero no prescribe sockets ni un protocolo de
red. Cada cliente representa un bando de una partida; las piezas son hilos
adicionales, no clientes adicionales.

## Matriz de trazabilidad

Las pruebas citadas describen la cobertura del repositorio. Su ejecución y
resultado deben consultarse en `pruebas.md` y en `evidencia/`; esta tabla no
constituye por sí sola un certificado de éxito.

| Fuente | Requisito | Módulo responsable | Prueba o comprobación |
| --- | --- | --- | --- |
| PDF §2.1 | Cuadrícula 5×5 acoplada al trono | `board.c` | `test_board.c`: topología y movimientos del trono |
| PDF §2.2 | Puma adyacente, salto, seis capturas para ganar | `board.c`, `game.c` | `test_board.c`: movimiento, captura y victoria |
| PDF §2.2 | Doce Perros sin captura; inmovilización del Puma | `board.c`, `game.c` | `test_board.c`: prohibición de captura y bloqueo con/sin salto |
| PDF §2.3 | Una pieza por posición y movimientos concurrentes sin turnos | `board.c`, `game.c` | `test_board.c`: 100 competencias por destino; `test_game.c` e integración 06/12: actividad |
| PDF §2.3 | Retardos aleatorios entre 50 y 300 ms | `config.c`, `game.c` | `test_config_queue.c` e integración 02: extremos/rangos; revisión del retardo con reloj monotónico |
| PDF §3 | `./torneo N M [config.txt]` | `main.c`, `config.c` | Integración 01/02: CLI, argumentos omitidos y valores inválidos |
| PDF §3.1 | Configuración de retardos, disposición y semilla | `config.c` | `test_config_queue.c`: duplicados, desconocidos, colisiones y desbordamiento |
| PDF §3.2 | N clientes concurrentes y emparejamiento de dos en dos | `tournament.c`, `queue.c` | Integración: comparar orden de llegadas con cada pareja y comprobar unicidad |
| PDF §3.2 | Máximo M mesas; espera pasiva cuando no hay cupo | `tournament.c` y condiciones | Integración 05/06/10/11/12: límite, cola y reutilización; revisión de `cond_wait` |
| PDF §3.3.1 | Juez independiente, tablero y primitivas inicializados | `game.c`, `tournament.c` | Integración 04/05/09; primitivas persistentes por slot, tablero preparado por emparejador y comprobado por juez |
| PDF §3.3.2 | Validación de adyacencia y captura | `game.c`, `board.c` | `test_board.c`: jugadas inválidas y captura atómica; revisión de revalidación en juez |
| PDF §3.3.3 | Matriz y eventos visibles en consola o log | `game.c`, `log.c` | Integración 06/12: movimientos; inspección de snapshots y eventos |
| PDF §3.3.4 | Detectar victoria, terminar y liberar mesa | `game.c`, `tournament.c` | Integración 04/05/10: posición terminal y nuevas parejas en el mismo slot |
| PDF §4.1 | Abandono produce victoria del oponente y limpieza | `tournament.c`, `game.c` | Integración 08 y `test_game.c`: abandono de ambos bandos y aviso obsoleto |
| PDF §4.2 | SIGINT detiene llegadas y cierra partidas/recursos | `signals.c`, `tournament.c`, `game.c` | Integración 06/07/11/12 y `test_game.c`: cierre en distintos estados |
| PDF §5 | Informe PDF ≤2 páginas sobre diseño, riesgos y evaluación | `docs/informe.pdf` | Revisión del documento y número de páginas |
| PDF §6 | C17, `-Wall -Wextra`, primitivas POSIX y ejecución Unix | `Makefile`, fuentes | Compilación real y suite completa |
| Preferencia de diseño | Mutex por tablero y FIFO real | `game.c`, `queue.c` | `test_board.c`, `test_config_queue.c` e integración FIFO |
| Preferencia de diseño | Hilo de señales con `sigwait()` | `signals.c` | Integración SIGINT y revisión de `join` del hilo |
| Petición directa | Archivos, ZIP, `.gitignore`, documentación y PR | Entrega y Git | Inventario, extracción del ZIP y referencia al PR |

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
5. **Capacidad.** Se limitan N a 1024 y M a 64 para acotar reservas e hilos. El
   límite es una política local, no una cifra exigida por el PDF. La falta de
   recursos del sistema puede provocar un error aun dentro de esos máximos y se
   atiende mediante limpieza de la inicialización parcial.
6. **FIFO.** Se utiliza el orden de inserción efectiva de clientes; el planificador
   puede hacer que un ID posterior se registre antes que uno anterior. El FIFO no
   garantiza la planificación justa de cada hilo ni de cada ficha.
7. **Abandono.** En un proceso con clientes locales, la desconexión se representa
   con una notificación cooperativa. La configuración de fallos facilita su
   evaluación. `fail_after_ms` comienza cuando el cliente observa su asignación;
   no es un plazo absoluto medido desde el primer mensaje del juez. No se afirma
   recuperación de una corrupción arbitraria de memoria o de la terminación
   forzada del proceso completo.
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
    vez por slot al iniciar el servidor. `game_prepare()` instala el tablero y
    estado de la siguiente partida, después de hacer `join` al juez anterior.
    El juez comprueba el estado, valida las jugadas y crea sus piezas. Esta
    distribución conserva los requisitos de inicialización y juez independiente
    del PDF sin destruir y reconstruir primitivas con cada pareja. Los recursos
    persistentes se destruyen al cerrar el servidor, después de recoger sus hilos.
12. **Orden de propuestas.** Cada pieza publica como máximo una propuesta
    pendiente, con secuencia protegida por el mutex del tablero. El juez procesa
    la de menor secuencia y revalida el destino. No se impone un turno de pieza:
    el orden nace de la concurrencia de las propuestas.

## Límites de la evidencia

Una prueba funcional con timeout puede detectar un bloqueo observado, pero no
demuestra ausencia de todos los interleavings problemáticos. AddressSanitizer y
ThreadSanitizer aportan evidencia en las ejecuciones que el entorno permita;
ninguno sustituye los invariantes ni una revisión del ciclo de vida. El informe
de pruebas debe distinguir ejecución satisfactoria, fallo del programa y fallo de
la herramienta o de su runtime. Las ejecuciones registradas de ASan/UBSan y TSan
omiten expresamente la inyección de `pthread_create` mediante `LD_PRELOAD`; esa
inyección se ejecuta en la suite normal. LeakSanitizer informó que el uso de
`ptrace` del entorno impide su ejecución, por lo que no se presenta como una
verificación dinámica de ausencia de fugas.

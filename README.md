# Komikan Concurrente

Torneo de Komikan en C17 y Pthreads para la Tarea 2 de Sistemas Operativos. El
programa modela clientes locales concurrentes, los empareja por llegada y ejecuta
hasta `M` partidas independientes. Un juez administra cada partida y crea trece
hilos de piezas: un Puma y doce Perros. Si el tablero ya es terminal al inicio,
no necesita crear esas piezas. No hay turnos.

## Compilar y ejecutar

Requiere Unix con GCC, Make y soporte POSIX Threads. Las pruebas de integración
requieren Python 3.

```sh
make
./torneo 2 1
./torneo 8 2 configs/ejemplo.txt
```

La compilación usa `gcc -Wall -Wextra -std=c17` y `-pthread`. La interfaz es:

```text
./torneo N M [config.txt]
```

`N` es el total de clientes y `M` el máximo de mesas activas. Ambos deben ser
enteros positivos. El límite de capacidad de esta implementación es 1024 clientes
y 64 mesas; se valida antes de reservar recursos. `M` puede superar la cantidad de
parejas disponibles. Con `N` impar, el último cliente que no encuentra rival se
retira sin disputar una partida. Estas decisiones resuelven aspectos no definidos
por el PDF.

Presiona **Ctrl+C** para detener llegadas, notificar partidas y esperar la salida
de todos los hilos. Los códigos de salida son 0 para ejecución normal, 1 para
fallos internos o falta de recursos, 2 para argumentos/configuración inválidos y
130 para cierre por `SIGINT`. Un fallo interno tiene prioridad si coincide con
una interrupción.

## Configuración

El archivo es texto `clave=valor`, con una clave por línea. Las claves son
opcionales y sus valores reemplazan la configuración predeterminada. Se rechazan
claves desconocidas o duplicadas, valores fuera de rango y posiciones repetidas.
Las líneas vacías y los comentarios indicados en el archivo de ejemplo se omiten.

| Clave | Significado |
| --- | --- |
| `min_delay_ms` | Retardo mínimo por intento de movimiento; entre 50 y 300 ms. |
| `max_delay_ms` | Retardo máximo; entre el mínimo y 300 ms. |
| `seed` | Semilla entera sin signo de 32 bits para los generadores locales. |
| `puma` | Nodo inicial del Puma, de 0 a 27. |
| `dogs` | Doce nodos iniciales de Perros, separados por comas. |
| `arrival_delay_ms` | Escalona la llegada del cliente `i` con `(i-1) × valor` ms, de 0 a 1000 ms por paso. |
| `fail_player` | ID de cliente que simula abandono durante su partida; 0 lo desactiva. |
| `fail_after_ms` | Espera desde que el cliente observa su asignación hasta notificar abandono, de 0 a 60000 ms. |

`fail_player` debe identificar un cliente de la ejecución. El abandono es una
extensión de prueba del modelo de clientes locales: no se abre un servidor de red.
La disposición predeterminada es Puma en 0 y Perros en 13 a 24; los retardos son
50–300 ms, la semilla es 1 y el abandono está desactivado. La semilla reproduce
las secuencias de cada generador; el orden de los hilos
depende del planificador y por tanto no garantiza una traza idéntica.

## Tablero y reglas adoptadas

Los nodos 0 a 24 representan la cuadrícula de 5×5, en orden de filas desde arriba.
Los nodos 25 y 26 son los puntos laterales del trono, y 27 es su vértice `T`.

```text
             27 (T)
          25       26
     0    1    2    3    4
     5    6    7    8    9
    10   11   12   13   14
    15   16   17   18   19
    20   21   22   23   24
```

Este esquema muestra IDs, no todas las aristas. Se respetan las conexiones
dibujadas en la página 2 del enunciado: la cuadrícula tiene enlaces horizontales
y verticales; el trono incorpora conexiones laterales, diagonales y la línea
central `27–2`. El cruce central del trono no tiene círculo y no se modela como
posición adicional. No se añaden diagonales dentro de la cuadrícula.

Una captura del Puma recorre dos aristas colineales hacia adelante, con un Perro
intermedio y destino vacío. La regla usa continuidad en el grafo; los segmentos
dibujados pueden tener distinta longitud en la conexión central del trono. El
PDF no detalla ese caso y esta interpretación queda explícita. Los Perros nunca
capturan. El Puma vence al capturar seis Perros; los Perros vencen cuando el Puma
carece tanto de pasos como de saltos legales.

## Concurrencia y cierre

La cola FIFO y los estados de clientes/mesas se protegen con el mutex del torneo.
El orden FIFO es el orden de registro efectivo bajo ese mutex, que puede diferir
del ID de cliente. Los clientes esperan con variables de condición. El
emparejador solo ocupa slots libres y por ello nunca supera `M` partidas activas.

Cada slot de mesa tiene un mutex y una condición persistentes, creados al
inicializar el servidor. Tras recoger mediante `join` al juez anterior, el
emparejador prepara el nuevo tablero y el mismo slot puede reutilizarse. El juez
comprueba la posición inicial, crea las piezas y procesa sus propuestas.

La comprobación de una jugada y su aplicación forman una única sección crítica
del tablero. Los trece hilos de piezas esperan retardos independientes; las
notificaciones permiten interrumpir esas esperas al terminar la partida. Las
propuestas tienen números de secuencia y el juez procesa primero la más antigua,
validándola de nuevo contra el estado actual. El juez publica estados
consistentes y libera el mutex del tablero antes de escribirlos en consola.

`SIGINT` se bloquea antes de crear hilos y lo recibe un hilo dedicado mediante
`sigwait()`. No se usa un manejador asíncrono ni `pthread_cancel()`. Se notifica el
cierre y se hace `join` antes de destruir mutexes y condiciones. `SIGUSR1` queda
reservada para despertar el hilo de señales al terminar normalmente.

Los movimientos aleatorios pueden prolongar una partida indefinidamente. El
enunciado no define empate ni tiempo máximo y el programa no inventa esas
condiciones; para detener una ejecución larga se utiliza `SIGINT`.

## Pruebas y documentación

```sh
make test
make asan
make tsan
make clean
```

`make test` compila las pruebas unitarias y ejecuta la suite de integración. Esta
cubre CLI/configuración, FIFO, partidas, reutilización, cliente impar, abandono,
fallos inyectados de `pthread_create` y SIGINT con llegadas o mesas en espera.
Las pruebas del tablero incluyen 100 competencias por una misma casilla; las de
partida comprueban abandono de ambos bandos y reutilización sin aceptar avisos
obsoletos.

`make asan` usa AddressSanitizer y UndefinedBehaviorSanitizer; `make tsan` usa
ThreadSanitizer. Estos objetivos reconstruyen el ejecutable y los tests con sus
opciones, por lo que conviene ejecutar `make clean && make` al volver al binario
normal. La inyección con `LD_PRELOAD` se omite expresamente en la integración
sanitizada para no superponer wrappers de `pthread_create`.

Los resultados ejecutados y sus límites se registran en `docs/pruebas.md` y los
logs de `docs/evidencia/`. La ejecución normal completó 12 pruebas de integración;
ASan/UBSan y TSan completaron 11 y omitieron la de inyección. LeakSanitizer no
pudo verificar fugas porque su runtime indicó que no funciona bajo `ptrace` en
este entorno. No se afirma una comprobación dinámica de cero fugas.

Para regenerar los entregables:

```sh
# Solo para regenerar el informe; no es dependencia del programa ni de los tests.
python3 -m pip install reportlab
make report
make zip
```

El PDF ya generado se entrega en `docs/informe.pdf`. `make report` requiere el
paquete Python ReportLab; `make zip` usa la biblioteca estándar de Python y crea
`komikan-concurrente.zip` en la raíz del repositorio. El ZIP incluye fuentes,
pruebas, configuraciones, documentación y un `MANIFEST.sha256`; su script verifica
la integridad del archivo y excluye ejecutables, objetos y temporales. Las fuentes
y configuraciones se conservan para
reproducir la compilación y las pruebas, sin prometer el mismo interleaving de
hilos en cada ejecución.

## Lectura del registro

`LLEGADA` identifica cliente y orden FIFO; `PAREJA` indica partida, mesa, bandos y
mesas activas. `MESA movimiento` incluye secuencia, ficha, origen, destino,
aceptación y capturas; los movimientos aceptados muestran el tablero actualizado.
`RESULTADO` indica el ganador: 1 Puma, 2 Perros, 0 interrupción y -1 error interno.
Los motivos principales son `capturas`, `inmovilizacion`, `abandono` y `SIGINT`.
`SIN_RIVAL` informa del cliente impar y `RESUMEN` permite comprobar el pico de
mesas, partidas completadas y cola restante. El registro se imprime por mensajes
completos, aunque el planificador puede alterar el orden entre eventos de mesas
distintas.

## Archivos

- `src/` y `include/`: implementación y contratos de los módulos.
- `configs/`: configuraciones de ejemplo y escenarios reproducibles.
- `tests/`: reglas, validación, FIFO y escenarios completos del proceso.
- `docs/requisitos.md`: trazabilidad al PDF y decisiones de interpretación.
- `docs/tablero.md`: grafo completo y reglas de salto del trono.
- `docs/defensa.md`: explicación de la solución y veinte preguntas con respuestas.
- `docs/informe.pdf`: informe de sincronización y riesgos, de hasta dos páginas.
- `tools/`: regeneración del informe y empaquetado de la entrega.
- `.github/workflows/`: comprobaciones preparadas para GitHub Actions; su ejecución
  remota se verifica por separado de los resultados locales.

El repositorio contiene fuentes y documentación. Los ejecutables, objetos y
archivos temporales se excluyen mediante `.gitignore`; se reconstruyen con Make.

## Descargar la entrega

El workflow «Compilar y probar» genera el artefacto **komikan-concurrente**.
En la ejecución de GitHub Actions del PR, la sección Artifacts permite descargar
`komikan-concurrente.zip`, con fuentes, pruebas, documentación y manifiesto SHA-256.
La descarga de este repositorio privado requiere iniciar sesión en GitHub.
También puedes generar exactamente el paquete local mediante `make zip`.

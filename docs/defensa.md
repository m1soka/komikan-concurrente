# Guía para comprender y defender la solución

## Recorrido de una ejecución

`main.c` valida `N`, `M` y el archivo opcional. La configuración se comprueba antes
de iniciar trabajo concurrente. Se bloquea `SIGINT` antes de crear hilos: todos
heredan esa máscara. Un hilo de señales espera con `sigwait()`; no ejecutamos
lógica compleja desde un manejador asíncrono.

Se crean clientes locales concurrentes. Cada cliente registra su llegada en una
cola FIFO protegida por el mutex del torneo y espera una notificación. El orden
de registro refleja qué hilo obtuvo el mutex primero, no necesariamente sus IDs.
El emparejador toma los dos elementos más antiguos cuando existe una mesa libre.
El primer cliente controla al Puma y el segundo a los Perros.

Al iniciar el servidor, `game_init()` crea un mutex y una condición para cada
slot de mesa. Cuando hay una pareja y un slot libre, el emparejador llama a
`game_prepare()` para instalar su tablero y estado. El juez comprueba la posición
inicial y, si no es terminal, crea trece hilos de piezas. El cliente del bando
Perros no es una ficha: sus doce fichas tienen actividad independiente. Cada
pieza espera un retardo aleatorio de 50 a 300 ms, elige una propuesta y permite
que el juez la valide contra el estado actual. Los movimientos de mesas distintas
no comparten mutex de tablero.

Cuando existe ganador, abandono o cierre administrativo, se marca un estado
terminal y se notifican todas las esperas. El juez espera el término de sus
piezas. Después se comunica el resultado a los clientes y se libera el slot de
mesa. El emparejador hace `join` al juez anterior antes de reutilizar su slot. Las
primitivas de ese slot se conservan entre partidas y se destruyen al terminar el
servidor, cuando ningún hilo puede volver a utilizarlas.

## Responsabilidades de los archivos

| Archivo o grupo | Responsabilidad |
| --- | --- |
| `src/main.c` | Interpretar la CLI, cargar configuración y devolver un código de salida. |
| `src/tournament.c` | Poseer clientes, cola y slots; crear el emparejador, limitar mesas y recoger hilos. |
| `src/game.c` | Administrar tablero, buzones de propuestas, hilos de piezas, juez, resultados y cierre. |
| `src/signals.c` | Bloquear señales y convertir `sigwait()` en una notificación normal de cierre. |
| `src/log.c` | Imprimir mensajes completos bajo un mutex independiente de salida. |
| `src/board.c` | Topología, jugadas legales, capturas, invariantes y condiciones de victoria. |
| `src/config.c` | Valores predeterminados y análisis estricto de configuración/enteros. |
| `src/queue.c` | FIFO circular de clientes; requiere exclusión del llamador si se comparte. |
| `include/*.h` | Contratos y tipos que separan responsabilidades entre módulos. |
| `tests/` | Comprobar reglas aisladas y comportamiento observable del proceso concurrente. |
| `Makefile` | Compilar con las opciones acordadas y ejecutar comprobaciones. |
| `configs/` | Mostrar el formato y permitir escenarios de juego o abandono. |
| `docs/pruebas.md` | Conservar comandos, resultados reales y limitaciones de verificación. |

La separación evita que el parser de configuración conozca Pthreads o que la
cola tenga que entender capturas. El tablero es lógica de dominio sin un mutex
interno: el módulo de partida protege las operaciones completas que lo usan.

## Estado compartido y exclusión mutua

El torneo comparte cola, estados de clientes, disponibilidad de mesas y petición
de cierre. Su mutex protege los cambios que deben verse juntos. Una variable de
condición no guarda el evento por sí sola: el estado protegido es el predicado
que cada hilo vuelve a comprobar después de despertarse.

Cada partida comparte ocupación de nodos, posición de piezas, capturas, propuestas
y estado terminal. Un único mutex de tablero evita que una validación observe un
estado que cambie antes de aplicar el movimiento. Si dos piezas reclaman el mismo
destino, solo la primera transacción legal puede ocuparlo; la siguiente valida de
nuevo y rechaza un destino ocupado. Los retardos liberan el mutex y la escritura
del snapshot en consola se realiza después de liberar el tablero.

Cada pieza tiene un buzón con una sola propuesta pendiente. Publicarla bajo el
mutex le asigna un número de secuencia. El juez procesa primero la secuencia más
antigua y la revalida, pues otra propuesta pudo haber cambiado el tablero. La
pieza espera que el juez consuma su propuesta antes de publicar otra. Este orden
evita adelantar propuestas ya pendientes sin convertir el juego en una ronda de
turnos por ficha.

Los generadores pseudoaleatorios son locales a cada hilo. De ese modo no se
comparte el estado interno de `rand()` ni se serializan todas las mesas por un
generador global. Los snapshots son copias consistentes que pueden escribirse
fuera de la sección crítica; el registro serializa mensajes completos para evitar
que sus líneas se mezclen.

## Legalidad de movimientos

La cuadrícula ocupa los nodos 0 a 24 en orden por filas. Los nodos 25 y 26 son los
puntos laterales del trono y el 27 es `T`. La topología se representa mediante
conexiones explícitas. El cruce central del trono sin círculo no se añade como
posición. En la cuadrícula no se dibujan diagonales.

Un paso legal sigue una arista y termina en un nodo vacío. Un Perro solo puede
hacer ese paso. El Puma también puede seguir dos aristas colineales hacia adelante
si el nodo intermedio contiene un Perro y el final está vacío. La eliminación del
Perro, el desplazamiento del Puma y el contador de capturas pertenecen a la misma
transacción protegida.

Después de cada cambio se evalúa el final: primero seis capturas del Puma; de lo
contrario, si el Puma no tiene ni pasos ni saltos, ganan los Perros. El orden evita
que una partida con seis capturas sea interpretada de otra manera por un estado
posterior. Una vez terminal, no se aceptan nuevas modificaciones del tablero.

## Riesgos y argumentos de diseño

**Carreras.** Validar una casilla sin proteger la aplicación permitiría dos
ocupantes o una captura sobre un estado obsoleto. La sección crítica incluye
comprobación y aplicación. Los indicadores de cierre también requieren la misma
disciplina de acceso que los demás estados compartidos.

**Deadlock.** No se espera un `join` manteniendo un mutex que el hilo esperado
necesita para salir. Las esperas de condición liberan el mutex de manera atómica y
lo recuperan al volver. Se evita adquirir mutex de torneo y de tablero a la vez;
si una notificación requiere ambos ámbitos, se captura primero la información y
se notifica después de soltar el primero. El registro no debe invertir ese orden.

**Livelock.** Los intentos inválidos no se repiten en un bucle inmediato: cada
ciclo incorpora un retardo y permite ejecutar otros hilos. Esto reduce contención,
pero el azar del juego puede producir ciclos legales prolongados.

**Starvation.** La cola evita adelantar clientes ya registrados cuando se asignan
parejas. POSIX no promete que cualquier mutex sea justo, así que no puede
garantizarse que toda ficha obtenga CPU en un plazo acotado. Las secciones
críticas breves y la espera pasiva reducen el riesgo; no lo convierten en una
garantía absoluta de equidad del planificador.

**Complejidad y rendimiento.** Un mutex por tablero es sencillo y suficiente para
28 posiciones; adquirir mutexes por casilla complicaría los saltos y su orden de
bloqueo. Una mesa serializa las modificaciones pequeñas, mientras decisiones,
retardos y otras partidas progresan en paralelo. Un registro abundante puede
dominar la latencia observada; no debe confundirse el rendimiento de consola con
el costo de la validación del tablero.

## Abandono, errores y SIGINT

La inyección de abandono identifica un cliente. Su espera `fail_after_ms` comienza
cuando ese cliente observa que ya fue asignado, no desde un reloj absoluto del
juez. La notificación termina la partida con victoria del rival y despierta al
juez. Las identidades de la pareja actual se verifican para que un aviso tardío
de la pareja anterior no termine una nueva partida en el mismo slot. Los hilos
no se cancelan mientras sostienen mutexes. La simulación cooperativa no pretende
recuperar un proceso corrompido o muerto por `SIGKILL`.

Un fallo al crear hilos o inicializar primitivas requiere recordar qué recursos
se inicializaron realmente. Solo esos recursos se destruyen; los hilos creados
reciben una petición de salida y se recogen. Nunca se hace `join` de un identificador
de hilo cuya creación falló.

Con Ctrl+C, el hilo de señales marca cierre, despierta al emparejador/clientes y
notifica las mesas. No se aceptan más llegadas una vez observado ese estado. Las
piezas salen de sus esperas sin esperar todo el retardo pendiente. Se recogen
clientes, piezas, jueces y el propio hilo de señales antes de liberar memoria y
primitivas. `SIGUSR1` se reserva para despertar `sigwait()` cuando la ejecución
termina normalmente.

## Veinte preguntas de interrogación

1. **¿Por qué no basta con un hilo por cliente?** Un cliente representa un bando;
   el PDF exige actividad independiente del Puma y de los doce Perros. Los hilos
   de piezas producen esa concurrencia dentro de cada partida.
2. **¿En qué instante se decide quién obtiene una casilla?** En la validación y
   aplicación bajo el mutex del tablero. El destino debe continuar libre en ese
   instante, aunque lo estuviera cuando se decidió la propuesta.
3. **¿Por qué `volatile` no sustituye al mutex?** No proporciona atomicidad ni
   orden de memoria entre hilos. Los accesos conflictivos deben sincronizarse.
4. **¿Por qué hay un mutex por tablero?** Cada partida comparte su estado, pero
   ninguna necesita bloquear otra mesa. Así se conserva independencia sin una
   jerarquía compleja de mutexes por posición.
5. **¿Cómo se evitan dos capturas del mismo Perro?** La presencia de la pieza
   intermedia, su eliminación y el contador se verifican y modifican en una sola
   sección crítica. El juez revalida cada propuesta; una segunda ve al Perro ya
   ausente aunque se hubiera decidido antes de la primera captura.
6. **¿FIFO significa IDs ascendentes?** No. Significa extraer en el mismo orden en
   que los clientes se registraron bajo exclusión mutua. La planificación puede
   alterar el orden de llegada de los IDs.
7. **¿Cómo se garantiza el máximo M?** Solo el emparejador reserva slots libres
   bajo el mutex del torneo; un slot no vuelve a estar disponible hasta el cierre
   correspondiente. No se crean partidas sobre slots ya ocupados.
8. **¿Qué pasa si la cola tiene suficientes clientes pero no hay mesa?** El
   emparejador espera en una condición. La liberación de mesa cambia el predicado
   y lo despierta; no consulta el estado en un bucle activo.
9. **¿Por qué se usa `while` alrededor de `pthread_cond_wait()`?** Puede haber
   despertares espurios o cambios hechos por otro hilo antes de recuperar el mutex.
   Solo el predicado protegido autoriza continuar.
10. **¿Qué hace realmente `pthread_cond_wait()` con el mutex?** Lo libera y entra
    en espera como una operación atómica respecto de la notificación; al regresar
    vuelve a poseerlo. El hilo debe entrar con el mutex adquirido.
11. **¿Dormir dentro del mutex sería incorrecto?** Bloquearía innecesariamente al
    resto de piezas. Los retardos se hacen mediante esperas que liberan el mutex
    o fuera de la sección crítica.
12. **¿Por qué la semilla no reproduce todos los movimientos?** Reproduce las
    secuencias locales de números, pero el orden de propuestas válidas cambia con
    la planificación y con el estado que ve cada hilo.
13. **¿Qué diferencia hay entre deadlock y una partida muy larga?** En un
    deadlock existe espera circular o progreso imposible por sincronización. En
    una partida larga pueden seguir registrándose movimientos legales sin alcanzar
    victoria. El juego aleatorio no ofrece tiempo de término acotado.
14. **¿Se garantiza ausencia de starvation?** La FIFO evita saltarse clientes en
    el emparejamiento; la API de mutexes no garantiza equidad temporal de cada
    pieza. Se reduce el riesgo con secciones breves y retardos, sin prometer una
    propiedad que el planificador no proporciona.
15. **¿Por qué no utilizar `pthread_cancel()` para el abandono?** La cancelación
    puede interrumpir un hilo en un punto con recursos retenidos. Una notificación
    cooperativa permite salir por rutas controladas y hacer `join` antes de limpiar.
16. **¿Por qué `sigwait()` en vez de un manejador de SIGINT?** El hilo puede usar
    primitivas normales de sincronización en contexto normal. Un manejador
    asíncrono no puede llamar libremente esas funciones de forma segura.
17. **¿Por qué se bloquea SIGINT antes de crear hilos?** Así todos heredan la
    máscara y la señal no termina por defecto un hilo cualquiera; la consume el
    hilo dedicado mediante `sigwait()`.
18. **¿Cuándo se puede destruir un mutex?** Cuando ningún hilo lo posee, espera
    por él o puede volver a usarlo. El orden de notificación y `join` establece ese
    momento; destruirlo inmediatamente al marcar cierre sería prematuro. En este
    diseño el slot conserva sus primitivas entre partidas y solo se destruyen al
    cerrar el servidor.
19. **¿Por qué el trono no es una extensión rectangular?** Su grafo tiene
    conexiones diagonales, laterales y central específicas del diagrama. Tratarlo
    como cuatro vecinos uniformes permitiría o prohibiría movimientos incorrectos.
20. **¿Una suite exitosa prueba que no hay ninguna carrera o fuga?** No demuestra
    todas las ejecuciones posibles. Aporta evidencia observable; se complementa
    con invariantes, revisión de propiedad de recursos y sanitizadores cuando el
    entorno los ejecuta correctamente. ASan/UBSan y TSan se ejecutaron sobre la
    suite sin el wrapper de inyección; LeakSanitizer no pudo validar fugas porque
    su runtime rechazó ejecutarse bajo `ptrace` en este entorno.

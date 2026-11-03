# Implementar y auditar Komikan Concurrente en C17

El repositorio inicial solo contenía un README. La entrega incorpora el torneo
completo: N clientes locales, emparejamiento FIFO, hasta M mesas, un juez por
partida y trece piezas concurrentes. Mantiene un mutex por tablero, condiciones
para espera pasiva, configuración estricta, logs y cierre mediante sigwait.

La auditoría corrige cuatro problemas: salida inesperada sin cleanup, tablero
inicializado por el emparejador, topes arbitrarios 1024/64 y bloqueo tardío de
SIGINT. Ahora el juez inicializa el tablero y adjudica fallos de participantes;
cleanup handlers liberan solo mutexes propios; los límites derivan de aritmética
y recursos; el receptor de señales precede a configuración y reservas. También
se corrige aceptación parcial de NUL en configuración. Las primitivas del slot
se conservan hasta que no quedan usuarios, sin reinicialización por partida.

Se preservó la lógica correcta de tablero y FIFO. Se verificaron las 28 posiciones,
48 aristas y 70 capturas dirigidas contra el diagrama, conservando explícita la
interpretación del salto central del trono. La matriz completa está en
`docs/requisitos.md` y los hallazgos en `docs/auditoria.md`.

Validación local ejecutada:

- `make clean && make`: ocho módulos C17 sin advertencias.
- `make test`: tres unitarios y 22/22 grupos de integración; además integración independiente 22/22.
- 2/10/100/200 clientes, reciclaje, FIFO, ocupación ≤ M, ENOMEM, fallos de creación/inicialización, abandono y salidas inesperadas.
- SIGINT con barreras en inicio, inicialización parcial y configuración, además de llegadas, mesas ocupadas y 200 clientes. Las señales anteriores al bloqueo inicial se clasifican por separado.
- `make asan` y `make tsan`: unitarios completos y 17 grupos aprobados, cinco omitidos explícitamente por LD_PRELOAD/RLIMIT_AS; sin reportes en las ejecuciones compatibles.
- Partida predeterminada con seis capturas y trece piezas unidas. PDF regenerado de dos páginas; ZIP con manifiesto SHA-256 y sin compilados/temporales.

LeakSanitizer local no puede verificar fugas bajo ptrace. El workflow actualizado
lo ejecuta en Ubuntu sobre unitarios e integración compatible, incluidos fallos
y SIGINT; la ejecución 41739284629 del commit de correcciones
4a986a1ce9a2ef3aba3f8bb3d96bda22840fd313 terminó con success en ambos jobs,
incluidos unitarios e integración compatible con LeakSanitizer activo. No se promete recuperación de SIGSEGV, SIGKILL, corrupción, cancelación
asíncrona ni cantidades ilimitadas de hilos. `docs/pruebas.md` separa resultados,
omisiones y límites.

Este PR queda abierto para revisión; no se fusiona automáticamente con main.

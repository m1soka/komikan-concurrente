# Implementar torneo de Komikan concurrente completo en C17

El repositorio inicial solo contenía un README. Esta entrega incorpora el torneo
con clientes locales concurrentes, cola FIFO, hasta M mesas, juez por partida y
13 hilos de piezas. El tablero sigue las 28 posiciones marcadas en el PDF y cada
mesa valida intenciones bajo su propio mutex. Incluye abandono cooperativo y
cierre mediante sigwait, notificación y join.

Se agregan ocho fuentes C, siete encabezados, Makefile, configuraciones, pruebas,
.gitignore, CI, informe PDF de dos páginas, trazabilidad y defensa oral con veinte
preguntas. El empaquetador produce un ZIP de fuentes con manifiesto SHA-256.

Validación real:

- `make clean && make test`: compilación GCC C17 sin avisos, tres ejecutables unitarios y 12 grupos de integración aprobados; incluye diez fallos inyectados de pthread_create.
- Carga de 100 clientes, reutilización y hasta cuatro partidas simultáneas; abandono y SIGINT antes del emparejamiento y con partidas/cola activas.
- Partida predeterminada completa: victoria del Puma con seis capturas y 13 piezas unidas.
- `make asan` y `make tsan`: aprobados; 11 grupos de integración compatibles y uno omitido explícitamente por inyección LD_PRELOAD, además de todos los unitarios.
- PDF verificado con dos páginas; ZIP extraído, hashes comprobados y fuentes compiladas/probadas desde la extracción.

LeakSanitizer no pudo verificar fugas: su runtime informa incompatibilidad con
ptrace. No se certifica cero fugas. Los detalles, interpretaciones del diagrama y
límites de verificación están en docs/pruebas.md y docs/requisitos.md. GitHub Actions también pasó los jobs test y sanitizers en la ejecución
41734201956, incluido un escenario de 100 clientes con LeakSanitizer activo.
El workflow publica el ZIP como artefacto descargable del PR.

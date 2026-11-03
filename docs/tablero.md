# Geometría y reglas implementadas

La fuente de requisitos es el PDF oficial, página 2. Se representan las
intersecciones con círculo del dibujo, conectadas únicamente por las líneas
dibujadas. Hay 28 posiciones: 25 en la cuadrícula y tres en el trono.

```text
                    27 (T)
                   / | \
                 25--|--26
                / |  |  | \
               00-01-02-03-04
               |  |  |  |  |
               05-06-07-08-09
               |  |  |  |  |
               10-11-12-13-14
               |  |  |  |  |
               15-16-17-18-19
               |  |  |  |  |
               20-21-22-23-24
```

En la cuadrícula, el identificador es `fila * 5 + columna`, con fila y columna
entre 0 y 4. Las coordenadas geométricas auxiliares del trono son 25=(-1,1),
26=(-1,3) y 27=(-2,2). Estas coordenadas solo sirven para comprobar la dirección
de los saltos. La configuración utiliza los identificadores 0..27.

El grafo tiene 48 aristas no dirigidas: 40 de la cuadrícula y ocho adicionales
del trono. Estas últimas son `27-25`, `27-26`, `27-02`, `25-26`, `25-00`,
`25-01`, `26-03` y `26-04`. No existen diagonales dentro de la cuadrícula.

El dibujo no marca un círculo donde la línea 25-26 cruza la línea 27-02.
Interpretamos ese cruce como líneas que se cruzan sin crear otra casilla.
Por ello no hay una posición número 29 ni conexiones 25-02 o 26-02.

Una ficha puede desplazarse a cualquier vecino libre del grafo. Un Puma
puede, además, recorrer dos aristas consecutivas en la misma dirección,
pasando sobre un Perro y aterrizando en una posición vacía. Se exige
colinealidad y avance; no se permite doblar en la posición intermedia.
El PDF no especifica si ambas aristas deben tener igual longitud geométrica.
Interpretamos la captura mediante adyacencias del grafo, por lo que también
son válidos los saltos 07-02-27 y 27-02-07. En ellos la conexión central del
trono es más larga, pero ambos pasos siguen la misma recta dibujada.

No se fuerzan capturas, cadenas de captura ni turnos, porque el enunciado no
los establece. Los perros nunca saltan ni capturan. Se comprueba la victoria
después de cada movimiento, incluida la captura: seis perros capturados dan
la victoria al Puma; carecer de movimientos legales, considerando también
los saltos, da la victoria a los Perros. La configuración inicial también
puede ser una posición terminal.

`board.c` mantiene dos representaciones coherentes: ocupante por posición y
posición por ficha. Un perro capturado tiene posición -1. `board_valid()`
comprueba que cada ficha viva ocupa una única posición, que ambos índices
coinciden y que perros vivos más capturados son exactamente 12.
El módulo no sincroniza por sí mismo: el dueño de la partida mantiene su
mutex para consultar, validar y aplicar cada movimiento como una operación
atómica. Esto permite que las mesas usen mutexes independientes.

La semilla fija las secuencias pseudoaleatorias de las fichas. No fija el
orden en que el planificador de hilos adjudica el mutex; las partidas
concurrentes pueden producir resultados diferentes con la misma semilla.

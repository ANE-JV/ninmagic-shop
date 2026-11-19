# Catálogo e interfaz de 0.1.3

La biblioteca abre Nintendo Switch y reserva diez tarjetas visibles, en cinco columnas y dos filas. La lista de consolas queda a la izquierda; el juego seleccionado aporta la imagen de fondo y los datos disponibles al panel derecho. Las coordenadas de dibujo, navegación táctil y comprobación de geometría proceden de `shop_layout.hpp`.

## Directorios antiguos

Algunos servidores ofrecen una carpeta por juego en la raíz. Mostrar esas carpetas directamente impedía ver las carátulas al elegir una consola. La vista consolidada conserva los paquetes del índice y añade una tarjeta para las carpetas que coincidan con un nombre base exacto y único en los metadatos del servidor. Una coincidencia ambigua o aproximada no sirve para vincular una instalación.

La tarjeta no contiene una descarga inventada. Al abrirla se lee únicamente su subárbol acotado y se validan los identificadores de los paquetes. La URL de una carpeta nunca se entrega al instalador como si fuera un NSP. Los identificadores propios de juegos retro conservan su identidad.

No se recorren todas las carpetas durante el inicio. Los directorios sin identidad segura permanecen disponibles en **Secciones → Ver carpetas del servidor**; la biblioteca indica que su vista consolidada es parcial. Elegir una consola vuelve a la raíz y a la vista de juegos. El buscador indica si se aplica a una consola, a la biblioteca o a una carpeta.

Si una carpeta opcional falla pero ya existe un paquete base verificado en la raíz, ese juego sigue accesible y su ficha indica contenido parcial. El filtro global de actualizaciones de estos listados sólo conoce las actualizaciones publicadas en el índice leído; la ficha de cada juego consulta además su carpeta.

Las plataformas y carátulas requieren información del servidor o del proveedor de imágenes. La disponibilidad de un archivo no permite deducir por sí sola su consola original. La clasificación descrita se aplica al índice JSON/HTML; no añade un filtro GraphQL que el servidor no publique.

## Comprobación

- Fixtures del parser: nombre exacto, nombres ambiguos, identificadores personalizados, paquetes independientes y exclusión de URL de carpetas del instalador.
- Regresión de red con la misma versión de libcurl del SDK, incluida la separación de conexiones del protocolo antiguo.
- Renderizado de PC con NanoVG y los mismos headers de dibujo del NRO, a 1280 × 720 y 1920 × 1080: diez tarjetas, selección de otro juego, texto largo, foco, carga, vacío, error y datos parciales.
- Compilación del NRO y comprobación de identidad, recursos, paquetes de distribución y transacciones del actualizador.

Los datos de almacenamiento, batería, consolas y disponibilidad usados en las capturas son fixtures. Las imágenes se obtuvieron del proveedor público por identificador. Las capturas de PC no acreditan ejecución física de esta versión, manejo real del mando ni una instalación completa en Switch.

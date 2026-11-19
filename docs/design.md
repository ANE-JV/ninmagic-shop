# Diseño de NinMagic Shop

## Alcance acordado

- Aplicación NRO separada de Cleaner, Cheats, Mods, Saves, Guardian y AIO.
- Logo NinMagic dentro de la aplicación y en el icono de hbmenu, con distintivo SHOP.
- Cualquier host compatible configurable: dominio o IP, puerto, ruta, usuario, contraseña y nombre del perfil. Admite varios perfiles.
- Usuarios administrados en el servidor existente; la aplicación no crea otro sistema de cuentas.
- Catálogo con carátulas y navegación por carpetas/plataformas cuando el servidor proporciona esa organización.
- Los juegos retro ya empaquetados en NSP se presentan como instalables y conservan su categoría de consola original cuando existe metadata o una carpeta que la identifica.
- Al preparar un juego se seleccionan la última actualización disponible y los DLC disponibles que falten. El usuario puede revisar los elementos antes de añadirlos a la cola.
- Actualizador de la aplicación conectado exclusivamente al repositorio `ANE-JV/ninmagic-shop`.

## Compatibilidad de catálogo

La API nativa de Ownfoil permite consultar juegos, versiones y DLC. Los índices JSON abiertos y listados de directorios antiguos aportan datos distintos; la aplicación usa únicamente los metadatos presentes y no transforma una página de inicio de sesión en un catálogo.

Una extensión NSP identifica el contenedor instalable, no la plataforma original del juego. Si el servidor no ofrece plataforma o carpeta, la aplicación no puede reconstruir con certeza esa clasificación ni fabricar una carátula ausente.

El host editable no implica compatibilidad con cualquier página web, sistema de pago o mecanismo de autenticación. El servidor debe ofrecer un formato admitido. Los índices cifrados destinados exclusivamente a otro cliente no se descifran aquí.

## Imágenes de la web existente

El proveedor autorizado por el propietario es `https://ninmagic.com/juegos`. Su buscador público admite una consulta por Title ID y publica los metadatos de los resultados en el bloque JSON `catalogGamesData`. Se usa una coincidencia exacta de identidad, incluyendo los identificadores de NSP retro; nunca una similitud aproximada del nombre para cambiar el destino de instalación.

La consulta de imágenes se realiza para las tarjetas visibles y conserva caché. Es independiente de los archivos descargables: no solicita juegos a esa web, no accede a páginas de contenido restringido ni necesita enviarle la contraseña del perfil Ownfoil. El origen de los enlaces de imagen puede ser SteamGridDB o el proveedor publicado por la web. La búsqueda pública no requiere una clave API de SteamGridDB en la aplicación.

Los perfiles permiten cambiar o retirar la dirección de metadatos. Si un proveedor no tiene una carátula para un identificador, se conserva el nombre del juego y la imagen predeterminada; no se bloquea por ello la navegación del catálogo.

## Referencia visual

El diseño aprobado es la referencia de implementación, con un lienzo lógico de 1280 × 720:

- Cabecera (0–88): logo, NINMAGIC SHOP, Mi cuenta, conexión, Wi-Fi y batería real.
- Lateral (30,108; 240 × 508): consolas, descargas, ajustes y espacio libre real.
- Centro (298–828): título, ubicación, búsqueda, carpetas y rejilla de dos columnas por dos filas, con tarjetas de 258 × 181.
- Panel derecho (844,108; 376 × 505): banner, contenido disponible, selección base/actualización/DLC y acciones de instalación.
- Pie (638–720): A/X cian, B rojo, Y amarillo y L/R/+ blancos.

La paleta compartida en `sphaira/include/ui/shop_style.hpp` fija el azul marino #030c18, cian #00eefc, violeta #9949ff y amarillo #ffc800. Los paneles, focos y botones tienen contornos luminosos dibujados por NanoVG. La fuente Barlow Bold, bajo OFL, conserva el peso de los encabezados; las fuentes del sistema cubren otros idiomas.

Las tarjetas y los checks representan el catálogo recibido y la selección real del instalador. La pantalla de cuentas conserva la misma estética y expone Editar tienda. Las imágenes conceptuales no sustituyen una prueba de ejecución en consola.

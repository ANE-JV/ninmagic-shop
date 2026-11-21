# Catálogo y carátulas de NinMagic Shop

Cada perfil guarda nombre, URL, puerto/ruta, URL alternativa, usuario y contraseña en `/config/NinMagicShop/ownfoil.ini`. El NRO y los paquetes públicos no incluyen perfiles ni credenciales. La clave se oculta en pantalla. El nombre local y los perfiles de distintas cuentas se conservan aunque el servidor sea el mismo.

La conexión automática prueba la API actual de Ownfoil, un índice JSON abierto y las carpetas HTML de Ownfoil clásico. HTTPS siempre verifica certificados. HTTP requiere escribir expresamente `http://`; no existe reintento automático de HTTPS por HTTP.

Pulsa **X** para buscar por nombre o ID dentro de la consola seleccionada. En la vista de carpetas la búsqueda se limita a la carpeta abierta. Para buscar en toda la biblioteca elige **Todas las consolas** en **Secciones**. **Y** abre la cola, **B** vuelve y **−** abre las opciones del catálogo y los favoritos.

El tipo de archivo NSP/NSZ/XCI/XCZ no identifica su consola original. La clasificación se toma de `platform`, `console` o `system`, de etiquetas explícitas como `[GBA]` o `[SNES]`, o de un componente completo de carpeta reconocido (por ejemplo `/PS1/`, `/Super Nintendo/`). Si falta, se muestra «Sin clasificar». Los IDs personalizados `05...` se reconocen para buscar metadatos, pero no se les inventa una relación de base/actualización/DLC por su sufijo. Un paquete sin relación explícita conserva su ficha y su instalación independientes.

Con el proveedor de imágenes de NinMagic, el cliente reconoce su colección NES al encontrar pares de identificador y título que la propia colección publica. La carpeta reúne los paquetes base de ese conjunto que aparecen en la tienda. Los datos de plataforma explícitos de cada título tienen prioridad. En GraphQL se agrupan las identidades por consola y sólo se piden las fichas completas de la página abierta.

Una ficha de juego con relaciones conocidas selecciona de forma predeterminada la base necesaria, la última actualización disponible y todos los DLC que falten. No se busca la relación por parecido del nombre. En un listado clásico estas relaciones se resuelven con los archivos presentes en la carpeta; un manifiesto puede enlazar archivos de otras rutas. No se rastrean miles de carpetas automáticamente.

## Actualización del contenido

Al conectar se vuelve a consultar el catálogo. «Actualizar catálogo», accesible en `Y` y en opciones, invalida los metadatos del índice y las carátulas web. Conserva las tarjetas visibles si falla la consulta. Las imágenes de la nueva página y las fichas posteriores vuelven a consultarse mediante GET condicional (ETag/Last-Modified si el servidor los ofrece), incluso cuando la URL de la carátula sigue siendo la misma. Una URL de imagen nueva genera otra clave de caché. No hace falta actualizar el NRO para que aparezcan juegos o imágenes nuevos.

El campo «Web de carátulas (búsqueda pública)» usa por defecto la web de NinMagic indicada por su propietario, y se puede editar o dejar vacío. Consulta `?q=ID` únicamente para las tarjetas visibles y la ficha abierta, comprueba el ID exacto devuelto y obtiene las URL de SteamGrid y la plataforma publicadas por esa web. No llama a la API privada de SteamGrid ni necesita una clave nueva. Hay como máximo dos trabajos de la página y dos peticiones nuevas por segundo; la caché positiva y negativa dura 15 minutos y está limitada a 1024 IDs. Una consulta pública se limita a 2 MiB y nunca recibe la contraseña/cookie del Ownfoil. No pagina páginas VIP ni intenta resolver Turnstile.

Las carátulas y plataformas pueden aparecer progresivamente al consultar las tarjetas. La clasificación aprendida por ID se conserva durante la sesión en una instantánea inmutable del índice. `Y` ofrece «Consolas detectadas» con el ámbito «Carpeta y metadatos consultados», y filtra esos registros conocidos sin pedir otras páginas VIP de la web. Un listado clásico sin plataforma no permite filtrar globalmente todas las consolas antes de obtener esos metadatos; las carpetas y un manifiesto completo sí proporcionan esa clasificación desde el principio. No se lanzan miles de búsquedas al conectar.

Los metadatos están limitados a 32 MiB y 100000 registros por documento; las imágenes, a 16 MiB. El saludo de la API se limita a 1 MiB y la consulta de actualizaciones tiene un máximo de 100 páginas de 1000 registros.

## Metadatos de la web existente

Ownfoil clásico no incluye imágenes en sus enlaces HTML. La aplicación puede completar esos enlaces mediante la API web existente `/api/titles`: usa la misma cuenta en `/login` y mantiene la cookie únicamente en memoria durante esa lectura. No crea cuentas ni modifica el catálogo. Las imágenes de otro origen reciben una petición sin la contraseña o cookie del Ownfoil.

El campo opcional «URL de carátulas web» admite un JSON existente con `games` o `files` (lista u objeto), y el bloque JSON público `catalogGamesData` de una página de NinMagic. El enlace es por ID de 16 dígitos exacto o por un campo `filename` exacto y no ambiguo. Nunca por similitud del título. Los campos de imagen aceptados incluyen `icon_url`, `iconUrl`, `coverUrl`, `cover`, `thumbnail` y `banner_url`/`bannerUrl`; pueden ser URL o un objeto con `url`. Las imágenes genéricas de placehold.it no cuentan como carátulas.

Ejemplo de metadatos suplementarios (dominios de ejemplo):

```json
{"games":[{"title_id":"0500123456780800","filename":"Example [0500123456780800].nsp","platform":"Super Nintendo","icon_url":"https://web.invalid/covers/example.png","banner_url":"https://web.invalid/banners/example.png"}]}
```

Ejemplo de catálogo JSON con archivos y metadatos:

```json
{"files":[{"url":"/packages/Example.nsp","name":"Example","platform":"PS1","coverUrl":"/covers/example.png"},{"url":"/retro/","directory":true,"name":"Retro"}]}
```

La página pública `/juegos` de NinMagic expone solamente los registros de su página visible. Leer esos datos no equivale a un índice global y no permite acceder a filtros, páginas o descargas que requieran sesión/VIP. Para cubrir todo el catálogo se necesita un endpoint autorizado que publique el conjunto correspondiente. Otro origen configurado se consulta sin reutilizar la cuenta o cookie del Ownfoil.

## Pruebas

`python tools/test-ownfoil.py --yyjson RUTA_AL_SRC_DE_YYJSON` compila y ejecuta las pruebas del parser de producción con un compilador del PC. Cubre JSON/HTML, tamaños de 64 bits, rutas/consolas, IDs personalizados sin agrupación inferida, paquetes sin ID independientes, arte por ID/filename exactos, nombres ambiguos, actualización de metadatos y JSON público de NinMagic. Los argumentos opcionales `--private-fixture` y `--private-metadata` leen capturas locales y sólo imprimen cantidades, nunca títulos o credenciales. Las pruebas no descargan ni instalan juegos.

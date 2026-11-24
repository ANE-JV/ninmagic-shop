# NinMagic Shop 0.1.9 — beta

Tienda para Nintendo Switch con Atmosphère. Acceso a tu servidor, catálogo con carátulas, búsqueda, favoritos y cola de instalación. Proyecto separado de las demás aplicaciones NinMagic.

Deriva de [Sphaira 1.1.1](https://github.com/NaGaa95/sphaira), cuyo cliente Ownfoil y motor de instalación se conservan y adaptan bajo GPL-3.0. Consulta [procedencia y licencias](docs/provenance.md).

## Instalación

Descarga [NinMagic-Shop-0.1.9.zip](https://raw.githubusercontent.com/ANE-JV/ninmagic-shop/main/releases/0.1.9/NinMagic-Shop-0.1.9.zip) y extráelo en la raíz de la microSD. La aplicación queda en:

```
sdmc:/switch/NinMagicShop/NinMagic-Shop.nro
```

También puedes ofrecer este ZIP en la carpeta de herramientas de NinMagic AIO Updater, configurado para extraer en la raíz de la microSD.

Abre hbmenu **en modo aplicación**, manteniendo R al abrir un juego, y entra en NinMagic Shop. El modo Álbum permite funciones limitadas; para instalar se necesita modo aplicación.

## Conectar tu tienda

1. Abre **+ → Cuenta y servidores** y añade un perfil.
2. Escribe el nombre del perfil, URL del servidor, puerto y ruta si hacen falta.
3. Introduce tu usuario y contraseña del servidor y guarda.
4. Conecta el perfil para consultar el catálogo.

Para corregir una tienda guardada, selecciónala y pulsa **Y — Editar tienda**. También puedes tocar **Mi cuenta** y abrir **Editar tienda actual** desde sus opciones. Los cambios se guardan en el mismo perfil; dejar la contraseña sin editar conserva la existente.

El host es editable. Se admiten varios perfiles, incluidos perfiles con distintas cuentas del mismo servidor. Las cuentas se administran en tu web/Ownfoil existente; la aplicación no crea usuarios nuevos.

La URL debe corresponder a un servidor compatible: API nativa de Ownfoil, índice JSON abierto o listado de directorios Ownfoil antiguo. Una página web cualquiera o un índice cifrado exclusivo de otro cliente no constituye una API compatible.

Se verifica el certificado y el nombre del servidor HTTPS. No se convierte automáticamente un error HTTPS en una conexión HTTP. Al escribir una URL HTTP explícita, esa conexión no dispone del cifrado de HTTPS.

## Biblioteca y controles

| Control | Función |
| --- | --- |
| A | Abrir una carpeta o la ficha del juego seleccionado. |
| B | Volver; en listados por carpetas, subir a la carpeta anterior. |
| X | Buscar por nombre o ID dentro de la consola o carpeta abierta. |
| Y | Abrir la cola; en la pantalla de cuentas, editar la tienda seleccionada. |
| − | Opciones del catálogo y del perfil. |
| + | Menú Shop: cuenta, cola, instalados, ajustes, actualizar y salir. |
| L / R | Cambiar de consola en la biblioteca. |

La biblioteca sigue el diseño de tres columnas: consolas, descargas y ajustes a la izquierda; buscador y diez carátulas visibles en el centro, distribuidas en cinco columnas y dos filas; información, fondo del juego seleccionado y botones de instalación a la derecha. Nintendo Switch aparece primero en el lateral y se selecciona al conectar. Los botones A/X son cian, B rojo, Y amarillo y L/R/+ blancos.

La cruceta y el stick izquierdo recorren los juegos. Desde el borde izquierdo se accede a las consolas y desde el derecho al contenido del paquete. Los controles visibles también responden al tacto. Las indicaciones de la parte inferior muestran las acciones disponibles en cada pantalla. Las fichas permiten consultar el contenido disponible y preparar la instalación.

La plataforma se obtiene de los metadatos o de la organización por carpetas del servidor. Los juegos retro que ya están empaquetados en **NSP** se pueden presentar como instalables, conservando su consola de origen cuando el catálogo aporta ese dato. Un paquete sin identificador conocido se mantiene separado de los demás; no se agrupan juegos distintos por compartir un identificador vacío.

Si el servidor ofrece un listado plano sin plataformas, los juegos siguen disponibles en «Todos» y los desconocidos se muestran como «Sin clasificar». Al consultar sus fichas y carátulas se incorpora la clasificación que publique la web. El filtro indica las consolas detectadas en los metadatos consultados; no rastrea todos los juegos del servidor para fabricar una clasificación global al arrancar.

Las carátulas proceden de las direcciones que ofrece el catálogo o la web del servidor y se guardan en caché en la microSD. Para los NSP retro, el proveedor público de imágenes se puede configurar con `https://ninmagic.com/juegos`: se consulta su búsqueda pública por el identificador exacto del juego y se reutilizan sus enlaces de SteamGridDB. Sólo se consultan los juegos visibles que necesitan carátula; no se recorren miles de páginas al iniciar. El proveedor se puede cambiar o vaciar en el perfil. La aplicación no inventa una carátula cuando el proveedor no aporta una dirección. Consulta [compatibilidad del catálogo](docs/design.md).

## Instalar un juego, actualización y DLC

Selecciona un juego y revisa la selección del panel derecho. «Instalar paquete» abre la revisión de instalación y «Añadir a la cola» encola la selección marcada. A abre la ficha completa. Cuando el servidor aporta sus identificadores y versiones, se propone la actualización más reciente disponible y los DLC que falten. Puedes cambiar la selección antes de añadirla a la cola.

El instalador procesa el paquete base, la actualización seleccionada y los DLC disponibles de ese mismo juego. Los archivos que contienen varios componentes se deduplican para no pedir la misma descarga varias veces. Los paquetes sin metadatos suficientes se muestran como archivos independientes: no se inventan relaciones entre ellos.

La cola permite consultar el progreso y los errores. Mantén la aplicación abierta mientras se instala. Los NSP retro que sean accesos directos pueden requerir archivos externos o un emulador que el autor del paquete haya previsto.

## Juegos nuevos del servidor

Al conectar la tienda se consulta el catálogo. Usa **Actualizar catálogo** para consultar los cambios sin cerrar la aplicación. Un juego o una carátula nuevos no requieren publicar otro NRO: deben estar disponibles en el índice o listado que exponga el servidor.

En API nativa la búsqueda puede consultar el catálogo del servidor. En listados antiguos, la vista por consolas reúne los archivos conocidos y las carpetas de juego que tienen una coincidencia exacta y única en los metadatos. El contenido de cada carpeta se comprueba al abrir el juego. Las carpetas que no se pueden identificar con certeza se conservan en «Carpetas», sin inventar su identidad. La interfaz indica cuándo el catálogo consolidado es parcial.

## Actualizar NinMagic Shop

En la versión 0.1.3 pulsa **+**, baja hasta **Buscar actualización** y confirma con **A**. Desde la versión 0.1.4 también puedes elegir **Actualizar app** en la biblioteca. La aplicación consulta este canal:

```
https://raw.githubusercontent.com/ANE-JV/ninmagic-shop/main/update.json
```

La consulta comprueba la versión publicada. Si ya tienes la misma versión, indica que no hay una actualización pendiente. Una versión nueva pide confirmación antes de descargar y sustituir el NRO.

La descarga se fija a un commit de este repositorio y comprueba tamaño, SHA-256 e identidad exacta del NRO. El reemplazo conserva un ejecutable anterior y un registro para recuperar una operación interrumpida. Después de actualizar, cierra y vuelve a abrir la aplicación.

## Archivos propios

- `switch/NinMagicShop/`: aplicación, caché y avisos de licencia.
- `config/NinMagicShop/`: perfiles, preferencias y estado del actualizador.

Los perfiles guardados contienen las credenciales en la microSD. No compartas esa carpeta como parte de un pack público. Los ZIP publicados contienen la aplicación y sus licencias, sin perfiles personales ni juegos.

## Cambios de 0.1.9

- La biblioteca muestra hasta 20 carátulas a la vez. Toca el botón de vista o pulsa R3 para cambiar a una lista de títulos. El panel de información ocupa menos espacio a la derecha.
- En la ficha de cada juego, **X · Votar** guarda o cambia tu voto de una a cinco estrellas en la microSD.
- El control que recibe el foco del mando se distingue por un contorno fluorescente que pulsa suavemente.
- El catálogo de NeroMagic agrupa sus colecciones retro por consola. Cada grupo incluye todas sus páginas y la búsqueda se limita a la consola seleccionada.
- Distingue Game Boy de Game Boy Color y Game Boy Advance. Los títulos de Nintendo 3DS y 2DS comparten una entrada. Los títulos de la serie ACA NEOGEO aparecen en Neo Geo y los de Arcade Archives aparecen en Arcade.
- La cruceta izquierda devuelve el foco a la lista de consolas también desde una página vacía.

## Cambios de 0.1.8

- Cada tienda puede leer un catálogo de enlaces directos desde la microSD. Los archivos se agrupan por título y versión al abrir el catálogo, así que la ficha puede mostrarlos sin pedir la carpeta de cada juego.
- El menú de instalación indica «Sin contenido por instalar» cuando la consola ya tiene todo lo seleccionado.

Consulta [cómo exportar y abrir un catálogo de la microSD](docs/catalog-format.md#catálogo-directo-desde-la-microsd).

## Cambios de 0.1.7

- La ficha presenta la carátula de la biblioteca desde que se abre cuando esa imagen ya está cargada. Los archivos del paquete se consultan en segundo plano.
- El panel distingue archivos en consulta, sin confirmar y ausentes. Cuando el catálogo entrega el juego base y su actualización, permite elegirlos mientras lee la información de la consola antes de revisar la instalación.
- El aviso 404 identifica el problema del enlace y permite distinguirlo de una descarga interrumpida.

## Cambios de 0.1.6

- Corrige el cierre al conectar con la tienda antes de mostrar la biblioteca. Los paquetes de publicación recompilan la aplicación completa.

## Cambios de 0.1.5

- El buscador conserva la consola o carpeta abierta. Para buscar «mario» en toda la biblioteca, elige **Secciones → Todas las consolas** antes de escribir.
- La carpeta **NES** reúne la colección de NinMagic desde el índice completo del servidor, aunque todavía no hayas abierto sus carátulas. Puedes recorrer todas sus páginas.
- Pulsa **A** sobre la carátula para abrir la ficha y elige **Instalar paquete**. La ficha reutiliza la consulta iniciada en la biblioteca. Si los archivos todavía se están comprobando, la revisión se abre en cuanto estén listos. Tú confirmas el contenido antes de instalarlo.
- A partir de la primera carga en esta versión, la app puede reutilizar las fichas guardadas en la microSD. Al volver a entrar verifica el acceso de la cuenta y lee el índice del servidor para detectar juegos añadidos. **Secciones → Actualizar catálogo** fuerza una consulta nueva.
- La tienda reconoce los listados HTML de Ownfoil con las cabeceras de cada versión. Los errores de descarga distinguen un enlace que el servidor devuelve como ausente, los permisos de la cuenta y las interrupciones de lectura.

## Cambios de 0.1.4

- Al bajar por la última fila o subir por la primera, la cuadrícula pasa a la página contigua y conserva la columna cuando hay un juego en ella. ZL y ZR siguen disponibles.
- La búsqueda de la biblioteca usa un índice reutilizable en los catálogos antiguos y admite nombres con o sin acentos. También encuentra carpetas coincidentes que el servidor aún tiene pendientes de identificar.
- Las carátulas se descargan, decodifican y reducen en segundo plano. La ficha de la derecha muestra el nombre inmediatamente y completa el género, la descripción y el fondo con los datos disponibles.
- La tienda y el proveedor configurado tienen prioridad. Para completar fichas e imágenes, la aplicación puede consultar Nlib por el identificador exacto del juego. Las consultas públicas de metadatos se hacen sin credenciales de la tienda.
- El botón **Actualizar app** está visible en la biblioteca y en **Tus tiendas**. El menú **+** también ofrece la actualización en su primera fila.

## Cambios de 0.1.3

- Consolas a la izquierda con Nintendo Switch primero, rejilla de cinco columnas por dos filas y navegación táctil/mando ajustada a esa distribución.
- Panel derecho con la imagen del juego como fondo, información real y selección de base, actualización y DLC.
- Vista consolidada para los servidores antiguos que ofrecen carpetas por juego. Sólo se vinculan carpetas con identidad comprobable y se valida su contenido antes de instalar.
- Skills ECC fijadas y guía de proyecto incluidas en las fuentes; revisión visual con los helpers de dibujo de producción.

## Corrección de transporte incorporada desde la prueba 0.1.2

- Las peticiones a Ownfoil antiguo abren una conexión TCP nueva y la cierran al finalizar. Corrige las cabeceras TE/Connection conservadas dentro de conexiones compartidas por libcurl 7.69.1, la versión del SDK de Switch.
- Se conserva la verificación HTTPS y la caché de DNS y sesiones TLS.
- Cada intento de conexión genera `config/NinMagicShop/connection-diagnostic.txt`, máximo 16 KiB, con fases, códigos, tipos de respuesta y nombres de cabeceras permitidas. No guarda la contraseña, el usuario, las URL ni el contenido de las respuestas.
- La regresión de red comprueba el aislamiento de las conexiones, además de sus cabeceras.

## Cambios de 0.1.1

- Biblioteca con la distribución, paleta neón y botones de colores del diseño aprobado.
- Edición visible de tiendas existentes, sin duplicar perfiles ni borrar la contraseña conservada.
- Corrección de la conexión con Ownfoil antiguo: sus peticiones usan un handle HTTP limpio para evitar las cabeceras TE/Connection persistentes que provocaban recibir la página de acceso web.
- Regresión con peticiones modernas y antiguas intercaladas; se mantiene la verificación HTTPS.

## Compilar y comprobar

En Windows, con devkitPro instalado:

```powershell
powershell -NoProfile -File tools/build.ps1
powershell -NoProfile -File tools/build.ps1 -Package
```

La compilación utiliza una carpeta temporal propia, un SDK libnx fijado y dependencias fijadas por revisión. No reemplaza el SDK global. `tools/verify.py` inspecciona el NRO, NACP, recursos y canal de actualización. `tools/package.py` construye el ZIP con un inventario permitido y los avisos de las dependencias.

Esta beta requiere pruebas en consola del recorrido completo: iniciar sesión, abrir carpetas, obtener carátulas, preparar un paquete, instalar y actualizar la propia aplicación. Una compilación correcta y las pruebas en PC no demuestran por sí solas que un juego concreto se instale o funcione en una consola.

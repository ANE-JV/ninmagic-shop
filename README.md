# NinMagic Shop 0.1.0 — beta

Tienda para Nintendo Switch con Atmosphère. Acceso a tu servidor, catálogo con carátulas, búsqueda, favoritos y cola de instalación. Proyecto separado de las demás aplicaciones NinMagic.

Deriva de [Sphaira 1.1.1](https://github.com/NaGaa95/sphaira), cuyo cliente Ownfoil y motor de instalación se conservan y adaptan bajo GPL-3.0. Consulta [procedencia y licencias](docs/provenance.md).

## Instalación

Descarga [NinMagic-Shop-0.1.0.zip](https://raw.githubusercontent.com/ANE-JV/ninmagic-shop/main/releases/0.1.0/NinMagic-Shop-0.1.0.zip) y extráelo en la raíz de la microSD. La aplicación queda en:

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

El host es editable. Se admiten varios perfiles, incluidos perfiles con distintas cuentas del mismo servidor. Las cuentas se administran en tu web/Ownfoil existente; la aplicación no crea usuarios nuevos.

La URL debe corresponder a un servidor compatible: API nativa de Ownfoil, índice JSON abierto o listado de directorios Ownfoil antiguo. Una página web cualquiera o un índice cifrado exclusivo de otro cliente no constituye una API compatible.

Se verifica el certificado y el nombre del servidor HTTPS. No se convierte automáticamente un error HTTPS en una conexión HTTP. Al escribir una URL HTTP explícita, esa conexión no dispone del cifrado de HTTPS.

## Biblioteca y controles

| Control | Función |
| --- | --- |
| A | Abrir una carpeta o la ficha del juego seleccionado. |
| B | Volver; en listados por carpetas, subir a la carpeta anterior. |
| X | Buscar juegos por nombre o una parte del nombre. |
| Y | Categorías, consolas y carpetas disponibles. |
| − | Opciones del catálogo y del perfil. |
| + | Menú Shop: cuenta, cola, instalados, ajustes, actualizar y salir. |
| L | Cola de instalaciones. |
| R | Juegos instalados / volver a la tienda. |

Las indicaciones de la parte inferior muestran las acciones disponibles en cada pantalla. Las fichas permiten consultar el contenido disponible y preparar la instalación.

La plataforma se obtiene de los metadatos o de la organización por carpetas del servidor. Los juegos retro que ya están empaquetados en **NSP** se pueden presentar como instalables, conservando su consola de origen cuando el catálogo aporta ese dato. Un paquete sin identificador conocido se mantiene separado de los demás; no se agrupan juegos distintos por compartir un identificador vacío.

Si el servidor ofrece un listado plano sin plataformas, los juegos siguen disponibles en «Todos» y los desconocidos se muestran como «Sin clasificar». Al consultar sus fichas y carátulas se incorpora la clasificación que publique la web. El filtro indica las consolas detectadas en los metadatos consultados; no rastrea todos los juegos del servidor para fabricar una clasificación global al arrancar.

Las carátulas proceden de las direcciones que ofrece el catálogo o la web del servidor y se guardan en caché en la microSD. Para los NSP retro, el proveedor público de imágenes se puede configurar con `https://ninmagic.com/juegos`: se consulta su búsqueda pública por el identificador exacto del juego y se reutilizan sus enlaces de SteamGridDB. Sólo se consultan los juegos visibles que necesitan carátula; no se recorren miles de páginas al iniciar. El proveedor se puede cambiar o vaciar en el perfil. La aplicación no inventa una carátula cuando el proveedor no aporta una dirección. Consulta [compatibilidad del catálogo](docs/design.md).

## Instalar un juego, actualización y DLC

Abre la ficha de un juego y revisa la selección. Cuando el servidor aporta sus identificadores y versiones, se propone la actualización más reciente disponible y los DLC que falten. Puedes cambiar la selección antes de añadirla a la cola.

El instalador procesa el paquete base, la actualización seleccionada y los DLC disponibles de ese mismo juego. Los archivos que contienen varios componentes se deduplican para no pedir la misma descarga varias veces. Los paquetes sin metadatos suficientes se muestran como archivos independientes: no se inventan relaciones entre ellos.

La cola permite consultar el progreso y los errores. Mantén la aplicación abierta mientras se instala. Los NSP retro que sean accesos directos pueden requerir archivos externos o un emulador que el autor del paquete haya previsto.

## Juegos nuevos del servidor

Al conectar la tienda se consulta el catálogo. Usa **Actualizar catálogo** para consultar los cambios sin cerrar la aplicación. Un juego o una carátula nuevos no requieren publicar otro NRO: deben estar disponibles en el índice o listado que exponga el servidor.

En API nativa la búsqueda puede consultar el catálogo del servidor. En el modo de directorios la búsqueda y los filtros se aplican a la carpeta que se está mostrando; la interfaz indica ese ámbito.

## Actualizar NinMagic Shop

Abre **+ → Buscar actualización**. El canal está incorporado y separado de las otras aplicaciones:

```
https://raw.githubusercontent.com/ANE-JV/ninmagic-shop/main/update.json
```

La consulta comprueba la versión publicada. Si ya tienes la misma versión, indica que no hay una actualización pendiente. Una versión nueva pide confirmación antes de descargar y sustituir el NRO.

La descarga se fija a un commit de este repositorio y comprueba tamaño, SHA-256 e identidad exacta del NRO. El reemplazo conserva un ejecutable anterior y un registro para recuperar una operación interrumpida. Después de actualizar, cierra y vuelve a abrir la aplicación.

## Archivos propios

- `switch/NinMagicShop/`: aplicación, caché y avisos de licencia.
- `config/NinMagicShop/`: perfiles, preferencias y estado del actualizador.

Los perfiles guardados contienen las credenciales en la microSD. No compartas esa carpeta como parte de un pack público. Los ZIP publicados contienen la aplicación y sus licencias, sin perfiles personales ni juegos.

## Compilar y comprobar

En Windows, con devkitPro instalado:

```powershell
powershell -NoProfile -File tools/build.ps1
powershell -NoProfile -File tools/build.ps1 -Package
```

La compilación utiliza una carpeta temporal propia, un SDK libnx fijado y dependencias fijadas por revisión. No reemplaza el SDK global. `tools/verify.py` inspecciona el NRO, NACP, recursos y canal de actualización. `tools/package.py` construye el ZIP con un inventario permitido y los avisos de las dependencias.

Esta beta requiere pruebas en consola del recorrido completo: iniciar sesión, abrir carpetas, obtener carátulas, preparar un paquete, instalar y actualizar la propia aplicación. Una compilación correcta y las pruebas en PC no demuestran por sí solas que un juego concreto se instale o funcione en una consola.

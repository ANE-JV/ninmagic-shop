# NinMagic Shop — guía del proyecto

Esta guía corresponde únicamente a `ninmagic-shop`, el cliente NRO de Ownfoil. Las peticiones actuales del usuario y las instrucciones superiores prevalecen sobre las recomendaciones de las skills.

## Skills ECC instaladas

Las fuentes originales se conservan en `.agents/skills/`, fijadas por `.agents/ecc-skills.lock.json` y con licencia en `.agents/ECC-LICENSE.txt`.

- C++ y gestión de recursos: `.agents/skills/cpp-coding-standards/SKILL.md`.
- Cambios de pruebas o investigación de regresiones: `.agents/skills/cpp-testing/SKILL.md`.
- Interfaz, jerarquía, imágenes, estados y revisión visual: `.agents/skills/frontend-design-direction/SKILL.md`.

Leer sólo las guías pertinentes al cambio. Los ejemplos web, CSS, GoogleTest o sanitizers se adaptan al proyecto; no obligan a incorporar frameworks o dependencias. Mantener las pruebas proporcionadas al cambio y las autorizaciones ya concedidas por el usuario.

## Arquitectura y compatibilidad

- Aplicación nativa C++/libnx, NanoVG/deko3d; no es una aplicación React ni una web.
- Conservar el estándar C++ configurado en CMake y las opciones `-fno-exceptions` y `-fno-rtti`. Usar el manejo de errores y las APIs C existentes; aplicar RAII a archivos, handles de curl, texturas y trabajadores.
- Los módulos siguen bajo `sphaira/` por su procedencia. Consultar `docs/provenance.md` y conservar GPL-3.0 y avisos.
- SDK aislado y dependencias fijadas: `tools/toolchain-lock.json`, `tools/dependency-lock.json` y `tools/build.ps1`. No sustituir el SDK global para seguir un ejemplo.
- Código principal: `sphaira/source/ui/menus/ownfoil.cpp`, `ownfoil_title.cpp`, `sphaira/source/utils/ownfoil_api.cpp`, `sphaira/source/download.cpp`, `sphaira/source/yati/` y `sphaira/source/shop_update*.cpp`.
- Dibujo compartido de la interfaz: `sphaira/include/ui/shop_style.hpp`, `shop_catalog_panel.hpp` y `shop_detail_panel.hpp`.

## Contrato visual vigente

La última petición del usuario actualiza el diseño anterior de cuatro tarjetas; contrastar siempre este contrato con el código antes de describirlo como implementado:

- Izquierda: consolas con su nombre, Nintendo Switch primero y después las demás consolas disponibles.
- Centro: al entrar en una consola, sus juegos con carátulas; al menos diez visibles a la vez, en cinco columnas y dos filas, cuando el catálogo contiene ese número.
- Derecha: información y acciones del juego seleccionado, con la imagen de ese juego como fondo; cambiar ambas al seleccionar otra carátula.
- Conservar el diseño aprobado azul oscuro, cian neón, violeta y amarillo, los botones A/X cian, B rojo, Y amarillo y L/R/+ blancos.

Mantener navegación de mando y táctil, foco visible, proporciones de imágenes, texto legible y estados de carga, vacío, error y datos parciales. Usar metadatos reales para plataformas, carátulas, versiones y DLC. Los NSP retro pueden tener identificadores propios; la extensión NSP no determina por sí sola su consola original.

## Verificación práctica

Ejecutar las comprobaciones relevantes existentes en `tools/` según el cambio:

- `test-transport.py` y `test-curl-legacy.py`: rangos HTTP y conexiones Ownfoil. La versión y opciones de libcurl deben corresponder al SDK; el fallo de conexiones compartidas de 7.69.1 no quedaba cubierto por una prueba sólo con 8.13.
- `test-ownfoil.py --yyjson <ruta-al-src-de-yyjson>`: parser, catálogo, planner y diagnóstico.
- `test-profiles.py --minini <ruta-a-minini>`: persistencia y edición de cuentas.
- `test-updater.py --yyjson <ruta-al-src-de-yyjson>`: transacciones y recuperación del actualizador.
- `verify.py <NRO>` y `test-verifier.py <NRO>`: identidad, estructura y recursos del ejecutable.
- `powershell -NoProfile -File tools/build.ps1 -Package`: compilación, gates y paquete con avisos de licencia.

El script de compilación usa un staging fuera del repositorio; las rutas de dependencias se encuentran en su `build-release/_deps/`. No añadir GoogleTest ni sanitizers al NRO por copiar los ejemplos de ECC. Las pruebas de host verifican comportamiento de host y no sustituyen mando, red, renderizado o instalación en Switch.

Para cambios visuales, inspeccionar el renderizado afectado con los mismos helpers de producción o en la consola. Identificar claramente las vistas de PC y los datos de ejemplo. El checker de evidencia de la skill comprueba la integridad del informe, no la verdad de sus observaciones. No marcar estados no probados como aprobados.

## Datos y distribución

- NRO canónico: `sdmc:/switch/NinMagicShop/NinMagic-Shop.nro`; perfiles y preferencias: `sdmc:/config/NinMagicShop/`.
- Preservar perfiles y datos. No imprimir credenciales, cabeceras Authorization/Cookie ni contenido privado de las respuestas. `.local/`, `private/` y copias de consola quedan fuera de Git y de los ZIP.
- Preservar verificación HTTPS, identidad de los paquetes y límites de destino del actualizador. No aceptar una página de login como catálogo ni fabricar relaciones entre juegos.
- Las versiones de `releases/<versión>/` son inmutables. Distinguir el código local, el binario copiado a la consola y el canal publicado en `update.json`.
- La publicación de una versión usa primero un commit de fuentes y artefactos, y después un commit del manifiesto que fija la descarga a aquel commit. Verificar la descarga pública y sus hashes.

Informar en español de lo que cambió y de la evidencia obtenida, distinguiendo pruebas automáticas, renderizado de PC y comprobación física.

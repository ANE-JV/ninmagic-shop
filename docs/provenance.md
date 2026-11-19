# Procedencia y licencias

NinMagic Shop es un proyecto independiente de NinMagic, derivado de Sphaira.

- Base: https://github.com/NaGaa95/sphaira
- Revisión inicial: `162a96d7109e2c4cd3abd138b8034272ce2e95c6` (Sphaira 1.1.1).
- Licencia de la aplicación y de las modificaciones: GPL-3.0, conservando el archivo `LICENSE` y los avisos de los autores originales.
- Sphaira aporta el renderizador NanoVG, cliente Ownfoil, cola y motor de instalación YATI. El nombre NinMagic Shop no atribuye a NinMagic la autoría original de esos componentes.
- El soporte de índices JSON y carpetas antiguas parte de las modificaciones GPL-3.0 de NinMagicFoil; ambas aplicaciones conservan ejecutables, cuentas locales y actualizadores separados.
- No se incorpora código GPL-2.0-only de NinMagic Guardian. El actualizador de Shop tiene implementación propia sobre esta base GPL-3.0.
- Las dependencias y sus revisiones están declaradas en CMake. El SDK libnx se prepara de manera aislada con el script de compilación; no es necesario sustituir el SDK global del usuario.

El icono propio se preparó a partir del logo NinMagic proporcionado por su propietario. El distintivo SHOP usa cian y ámbar para diferenciar esta aplicación. La imagen conceptual del catálogo es una propuesta visual, no una captura de una consola ni una prueba de instalación.

Este repositorio no incluye juegos, archivos de partidas, claves de consola ni credenciales de servidores. Las fuentes correspondientes y las instrucciones de compilación se distribuyen junto con la aplicación.

# Corrección de conexiones Ownfoil para 0.1.2

La versión 0.1.1 se reprodujo y corrigió inicialmente con libcurl 8.13.0. La consola usa libcurl 7.69.1, con TLS libnx y zlib. En esta versión antigua el bloque TE pertenece a `connectdata::allocptr`, que puede volver a utilizarse a través de la caché `CURL_LOCK_DATA_CONNECT` compartida aunque el easy handle sea nuevo.

Se compiló libcurl 7.69.1 con zlib 1.3.1 en una carpeta de investigación aislada y se repitió la función de opciones de producción con el mismo perfil obtenido de la consola. No se modificó el servidor.

| Secuencia HTTP/1.1 | Política anterior | Política 0.1.2 |
| --- | --- | --- |
| Primer legacy limpio | 200, 887826 B, 15363 enlaces | 200, 887826 B, 15363 enlaces |
| Moderno → easy nuevo legacy con share CONNECT | 200, 3172 B, formulario de acceso | 200, 887826 B, 15363 enlaces |
| OPTIONS → moderno → legacy | 200, 3172 B, formulario de acceso | 200, 887826 B, 15363 enlaces |

En la política anterior las peticiones contaminadas seguían enviando TE y Connection. `ConfigureLegacyConnection` establece ahora FRESH_CONNECT y FORBID_REUSE sólo para las peticiones legacy. Se conserva la verificación del certificado y del nombre HTTPS, junto con las cachés compartidas de DNS y sesiones TLS.

La regresión local usa el código de producción y comprueba `CURLINFO_NUM_CONNECTS == 1` por petición legacy. Esto detecta una pérdida del aislamiento también cuando el libcurl host reciente no reproduce el estado interno de 7.69.1. El control sin aislamiento reutiliza la conexión (`NUM_CONNECTS == 0`). Las credenciales de esa prueba permanente son sintéticas.

## Informe de conexión

`config/NinMagicShop/connection-diagnostic.txt` se sustituye al iniciar una conexión y tiene un límite de 16 KiB. Recoge la fase, códigos de transporte y HTTP, versión de libcurl, nombres de cabeceras permitidas, clases de endpoint y un resumen estructural del cuerpo. No persiste URLs, cuentas, contraseñas, cookies ni valores de cabeceras. El análisis del cuerpo está limitado a los primeros 64 KiB; el informe distingue ese número del tamaño total recibido.

La salida verbose de libcurl se dirige únicamente a un callback que filtra los nombres de cabeceras. La opción verbose permanece desactivada si el callback no se pudo instalar. No se registra contenido de tráfico.

El informe se crea aunque el registro general esté desactivado. La generación de cada intento impide que trabajadores antiguos escriban en el informe de una nueva conexión. Un fallo al guardar el informe no transforma un fallo de red en éxito ni altera la aceptación del catálogo.

Referencias primarias: [fuente HTTP de curl 7.69.1](https://github.com/curl/curl/blob/curl-7_69_1/lib/http.c), [FRESH_CONNECT](https://curl.se/libcurl/c/CURLOPT_FRESH_CONNECT.html), [FORBID_REUSE](https://curl.se/libcurl/c/CURLOPT_FORBID_REUSE.html).

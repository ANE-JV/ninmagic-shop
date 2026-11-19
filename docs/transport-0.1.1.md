# Corrección del catálogo legacy para 0.1.1

El fallo de 0.1.0 se reprodujo con libcurl nativo y la función
`SetCommonCurlOptions` extraída de `download.cpp`. Un handle limpio obtenía el
listado correcto. Después de una petición moderna, el mismo handle conservaba
las líneas internas `TE` y `Connection` al ejecutar `curl_easy_reset` y cambiar
al perfil legacy, aunque `CURLOPT_TRANSFER_ENCODING` se estableciera en cero.
El servidor respondía con HTTP 200 y una página de inicio de sesión, que el
analizador rechazaba como un listado inválido.

La prueba contra el servidor configurado produjo estos resultados, sin imprimir
credenciales ni nombres del catálogo:

| Petición HTTP/1.1 | Bytes | Enlaces | Tablas | Formularios |
| --- | ---: | ---: | ---: | ---: |
| Legacy con handle limpio | 887826 | 15363 | 1 | 0 |
| Moderna → reset → legacy, antes del cambio | 3172 | 0 | 0 | 1 |
| Moderna → reset → legacy, con aislamiento | 887826 | 15363 | 1 | 0 |

La supresión explícita mediante `User-Agent:`, `TE:` y `Connection:` no bastó
para eliminar las líneas que el handle ya había generado. Cada petición legacy
usa ahora un easy handle nuevo; conserva la caché compartida de conexiones,
DNS y sesiones TLS, y vuelve a aplicar la verificación del certificado y del
hostname. Los encabezados observados en la petición correcta fueron solamente
`Host`, `Accept`, `Accept-Encoding` y `Authorization`.

El caso local permanente usa un servidor HTTP/1.1 en loopback y credenciales
ficticias. Compila la función real de opciones de producción y el mismo helper
de handles. Comprueba el primer acceso, OPTIONS/moderno → legacy y un segundo
acceso legacy. También muestra el control sin aislamiento para identificar la
contaminación cuando la versión de libcurl la reproduce:

```text
python tools/test-curl-legacy.py
```

La reproducción nativa se ejecutó con libcurl 8.13.0 y el transporte modificado
se compiló como objeto ARM con el SDK Switch y sus cabeceras libcurl 7.69.1.
El servidor y la consola no se modificaron; no se instaló contenido.

Referencias de libcurl: [HTTPHEADER y supresión de encabezados](https://curl.se/libcurl/c/CURLOPT_HTTPHEADER.html),
[estado conservado por easy_reset](https://curl.se/libcurl/c/curl_easy_reset.html)
y [TRANSFER_ENCODING](https://curl.se/libcurl/c/CURLOPT_TRANSFER_ENCODING.html).

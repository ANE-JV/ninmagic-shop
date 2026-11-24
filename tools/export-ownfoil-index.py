#!/usr/bin/env python3
"""Export Ownfoil 2.3 package links from the shop owner's SQLite database."""
import argparse
import itertools
import json
import os
from pathlib import Path
import re
import sqlite3
import sys
import tempfile
from urllib.parse import quote, urlsplit


MAX_ROWS = 65_536
MAX_BYTES = 16 * 1024 * 1024
EXTENSIONS = {".nsp", ".nsz", ".xci", ".xcz"}
ASSOCIATIONS = """
    SELECT f.id, f.filename, f.filepath, f.size, f.identified, f.multicontent,
           a.app_id, a.app_version, a.app_type, a.owned, t.title_id
      FROM files AS f
      LEFT JOIN app_files AS af ON af.file_id = f.id
      LEFT JOIN apps AS a ON a.id = af.app_id
      LEFT JOIN titles AS t ON t.id = a.title_id
"""
TITLE_FILTER = """
     WHERE f.id IN (
           SELECT af.file_id FROM titles AS t
             JOIN apps AS a ON a.title_id = t.id
             JOIN app_files AS af ON af.app_id = a.id
            WHERE t.title_id = ? COLLATE NOCASE)
"""


def title_id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[0-9a-fA-F]{16}", value):
        return None
    return value.upper() if int(value, 16) else None


def server_url(value):
    if not value or len(value.encode("utf-8")) > 2048 or any(c.isspace() or ord(c) < 32 or ord(c) == 127 for c in value):
        raise ValueError("Introduce una dirección HTTPS válida para el mismo servidor de Ownfoil.")
    address = urlsplit(value)
    if (address.scheme != "https" or not address.hostname or address.username is not None
            or address.password is not None or "%" in address.netloc or any(c in value for c in ("?", "#", "@", "\\"))):
        raise ValueError("Usa la URL HTTPS de la tienda, sin usuario, contraseña ni parámetros.")
    if address.port == 0:
        raise ValueError("El puerto del servidor es inválido.")
    return value[:-1] if value.endswith("/") else value


def package(row, identities):
    file_id, filename, filepath, size, identified, multicontent = row[:6]
    if identified != 1 or multicontent or len(identities) != 1:
        return None
    app, version, kind, owned, parent = next(iter(identities))
    app, parent = title_id(app), title_id(parent)
    if not app or not parent or owned != 1 or kind not in ("BASE", "UPDATE", "DLC"):
        return None
    if (kind == "BASE") != (parent == app):
        return None
    if not isinstance(file_id, int) or file_id <= 0 or not isinstance(size, int) or size <= 0:
        return None
    if not re.fullmatch(r"[0-9]{1,10}", str(version)) or int(version) > 0xffffffff:
        return None
    if (not isinstance(filename, str) or not isinstance(filepath, str)
            or not filename or any(c in filename for c in ("/", "\\"))
            or any(ord(c) < 32 or ord(c) == 127 for c in filename)
            or Path(filename).suffix.lower() not in EXTENSIONS
            or filepath.replace("\\", "/").rsplit("/", 1)[-1] != filename):
        return None
    url = f"/api/get_game/{file_id}#{quote(filename, safe='')}"
    if len(url) > 4096:
        return None
    return {
        "url": url,
        "app_id": app, "title_id": parent, "version": int(version),
        "kind": kind.lower(), "size": size,
    }


def export_index(database, server, selected):
    # mode=ro fails if the file is missing and honours live SQLite transactions.
    # Keep one statement open so every package belongs to the same snapshot.
    connection = sqlite3.connect(database.resolve().as_uri() + "?mode=ro", uri=True)
    data = bytearray(b'{"schema":1,"server":' + json.dumps(server).encode() + b',"files":[\n')
    exported = skipped = 0
    try:
        connection.execute("PRAGMA query_only=ON")
        sql = ASSOCIATIONS + (TITLE_FILTER if selected else "") + " ORDER BY f.id"
        rows = connection.execute(sql, (selected,) if selected else ())
        for _, group in itertools.groupby(rows, key=lambda row: row[0]):
            first = next(group)
            # Apply the title filter to file IDs first and still consider every
            # association. Filtering away another game's row could mislabel a bundle.
            identities = {first[6:]}
            identities.update(row[6:] for row in group)
            entry = package(first, identities)
            if entry is None:
                skipped += 1
                continue
            encoded = json.dumps(entry, separators=(",", ":")).encode("utf-8")
            separator = b",\n" if exported else b""
            if exported >= MAX_ROWS or len(data) + len(encoded) + len(separator) + 4 > MAX_BYTES:
                raise ValueError("El índice supera el límite de NinMagic Shop. Exporta por juego con --title.")
            data.extend(separator)
            data.extend(encoded)
            exported += 1
    finally:
        connection.close()
    if not exported:
        raise ValueError(f"Ningún paquete exportable para esta selección ({skipped} archivos omitidos).")
    data.extend(b"\n]}\n")
    return data, exported, skipped


def write_output(path, database, data):
    # Never let an output path replace SQLite's database or its live sidecars.
    db = database.resolve()
    protected = (db, *(Path(str(db) + suffix) for suffix in ("-wal", "-shm", "-journal")))
    destination = path.resolve()
    if any(destination == other or (destination.exists() and other.exists() and destination.samefile(other))
           for other in protected):
        raise ValueError("El destino debe ser un archivo diferente de la base de datos y sus archivos auxiliares.")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix=".ninmagic-index-", delete=False) as output:
            temporary = Path(output.name)
            output.write(data)
        os.replace(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", "--db", type=Path, default=Path("/app/config/ownfoil.db"),
                        help="base SQLite de esta instancia (por defecto /app/config/ownfoil.db)")
    parser.add_argument("--server", required=True, help="URL HTTPS pública de esa misma instancia")
    parser.add_argument("--title", help="exportar solo los paquetes de este ID de juego base (16 hexadecimales)")
    parser.add_argument("--output", type=Path, help="ruta del índice JSON (por defecto, salida estándar)")
    args = parser.parse_args()
    try:
        server = server_url(args.server)
        selected = title_id(args.title)
        if args.title is not None and selected is None:
            raise ValueError("--title requiere un ID hexadecimal de 16 caracteres distinto de cero.")
        data, exported, skipped = export_index(args.database, server, selected)
        if args.output is None:
            sys.stdout.buffer.write(data)
        else:
            write_output(args.output, args.database, data)
    except (OSError, sqlite3.Error, ValueError) as error:
        parser.exit(1, f"Error al exportar: {error}\n")
    print(f"{exported} paquetes exportados; {skipped} archivos ambiguos o sin datos válidos omitidos.", file=sys.stderr)


if __name__ == "__main__":
    main()


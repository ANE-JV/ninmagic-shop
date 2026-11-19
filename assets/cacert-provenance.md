# Certificate authority trust store

`romfs/cacert.pem` is the Mozilla CA bundle converted to PEM by curl.

- Source: https://curl.se/ca/cacert.pem
- Retrieved: 2026-11-19 using authenticated HTTPS.
- Bytes: 200116
- SHA-256: `427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285`
- License: Mozilla Public License 2.0; preserved in `third_party/licenses/Mozilla-MPL-2.0.txt`.

The app stages this exact bundle to its own cache before network workers start. Every connection checks its size and hash, verifies the peer certificate and hostname, and rejects invalid trust state. HTTPS redirects cannot downgrade to HTTP. Custom server URLs are supported without disabling verification.

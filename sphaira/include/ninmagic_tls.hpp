#pragma once

#include <curl/curl.h>
#include <switch.h>

namespace ninmagic::tls {
inline constexpr char TrustStorePath[] = "sdmc:/switch/NinMagicShop/cache/cacert.pem";
inline constexpr char TrustStoreSha256[] = "427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285";

// Call on the main thread before starting any worker that uses ROMFS or curl.
// ROMFS is unmounted before returning, including on failure.
auto PrepareTrustStore() -> Result;
// Verifies the cached bundle against the embedded pin before each connection.
// No insecure fallback is enabled when verification/configuration fails.
auto ConfigureCurlTrust(CURL* handle) -> bool;
}

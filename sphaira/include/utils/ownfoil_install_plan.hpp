#pragma once
#include "utils/ownfoil_index.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace sphaira::ownfoil::plan {
struct Version {
    uint64_t version{};
    std::string app_id{};
    bool base{};
    bool known{true};
};
struct Installed { bool present{}; uint64_t version{}; };
enum class File { Base, Chosen };
struct Selection { File file; uint64_t app_id{}; };

inline auto Offered(const Version& chosen, const Installed& held, bool allow_downgrade) -> bool {
    // Explicit UPDATE metadata can omit its version or legitimately report 0.
    // The installer reads the real CNMT; neither case proves it is installed.
    if (!held.present || !chosen.known || (!chosen.base && chosen.version == 0) || chosen.version >= held.version) return true;
    return chosen.version > 0 && allow_downgrade;
}
inline auto Needed(const Version& chosen, const Installed& held, bool allow_downgrade) -> bool {
    if (!Offered(chosen, held, allow_downgrade)) return false;
    return !held.present || !chosen.known || (!chosen.base && chosen.version == 0) || chosen.version != held.version;
}
inline auto Build(uint64_t parent, const Version& chosen, const Version& base, const Installed& held, bool allow_downgrade) -> std::vector<Selection> {
    if (!Needed(chosen, held, allow_downgrade)) return {};
    if (chosen.base) return {{File::Chosen, index::InstallTarget(parent, chosen.app_id, true)}};
    std::vector<Selection> out;
    if (!held.present && base.base) out.push_back({File::Base, index::InstallTarget(parent, base.app_id, true)});
    out.push_back({File::Chosen, index::InstallTarget(parent, chosen.app_id, false)});
    return out;
}
} // namespace sphaira::ownfoil::plan

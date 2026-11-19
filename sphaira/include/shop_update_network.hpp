// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace sphaira::shop_update {
struct TransferHooks {
    std::function<bool()> cancelled;
    std::function<void(uint64_t current, uint64_t total)> progress;
};
// Hooks are private to the worker calling this function. Reset them before the
// worker exits; never tear down the application's shared curl/socket services.
void setUpdateTransferHooks(TransferHooks hooks);
bool downloadHttpsFile(const std::string& url, const std::string& path, std::string& error);
void updateNetworkExit();
}

#pragma once

struct SplatModHost;

namespace coop_hooks {

bool install(const SplatModHost* host);
void uninstall(const SplatModHost* host);

} // namespace coop_hooks

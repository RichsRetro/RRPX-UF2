#pragma once
#include <string>
namespace setup {
struct RuntimeResult {bool ok=false;std::string message,launchPath;};
bool shouldAutoStartSession();
RuntimeResult stageRuntimePatches();
RuntimeResult restoreRuntimeStage();
}

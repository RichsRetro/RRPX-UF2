#pragma once
#include <string>
namespace startupProgress {
void run();
bool recoveryRequested();
const std::string& lastError();
}

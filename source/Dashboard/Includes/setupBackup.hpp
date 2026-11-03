/* SPDX-License-Identifier: MIT — RRXD installer backup engine, 2026. */
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include "3rdparty/json.hpp"
namespace setup {
struct BackupInput {
  std::string source, parent, executable;
  std::array<unsigned char,256> eeprom;
  nlohmann::json console;
};
struct BackupResult { bool ok=false; std::string folder, error; uint64_t bytes=0; unsigned files=0; };
using Progress=std::function<bool(const std::string&,uint64_t)>;
BackupResult createBackup(const BackupInput&,const Progress&);
bool verifyBackup(const std::string&,std::string&);
std::string hashFile(const std::string&,uint64_t* bytes=nullptr);
std::string findLatestBackup(const std::string& parent);
// Caller must first verify the complete backup and its live console identity.
bool sourceMatchesVerifiedBackup(const std::string& folder,const std::string& source,std::string& error);
}

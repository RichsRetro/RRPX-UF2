#include "dashboardPaths.hpp"

#include <algorithm>

#ifdef NXDK

#include <windows.h>
#include <nxdk/mount.h>

#endif

namespace { std::string rootPath; bool structured = false; }

void initializeDashboardPaths() {
#ifdef NXDK
  // Separate settings/data folder. No installed main-dashboard paths change.
  if (!nxIsDriveMounted('E')) nxMountDrive('E', "\\Device\\Harddisk0\\Partition1");
  CreateDirectoryA("E:\\RRXD-Lite-UF2", nullptr);
  rootPath = "E:\\RRXD-Lite-UF2\\";
  structured = false;
#else
  rootPath = "./";
#endif
}

std::string dashboardAssetPath(const char* name) {

  return rootPath + (structured ? "assets\\" : "") + name;

}

std::string dashboardConfigPath() {

  return rootPath + (structured ? "config\\config.json" : "config.json");

}

std::string dashboardDataPath(const char* name) {

  std::string path = rootPath + name;

#ifdef NXDK

  std::replace(path.begin(), path.end(), '/', '\\');

#endif

  return path;

}


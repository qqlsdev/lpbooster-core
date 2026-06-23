#include "includes/lpbooster.hpp"
#include <chrono>
#include <filesystem>
#include <format>
#include <unistd.h>

std::string getTimeNow() {
  auto now = std::chrono::system_clock::now();
  return std::format("[{0:%F : %T}] ",
                     std::chrono::floor<std::chrono::seconds>(now));
}

int main() {

  FileAssist file_assist;
  Backup::BackupManager backup(file_assist);
  LpBooster::Tools tools(file_assist);
  LpBooster::Tools::Results result;

  auto msg = [&](const std::string_view &msg) {
    std::string logMsg = getTimeNow() + std::string(msg);

    const std::string logPath = "/var/lib/lpbooster/logs.log";
    if (!fs::exists(logPath)) {
      if (!file_assist.createFile(logPath).is_ok()) {
        return false;
      }
    }
    if (msg.find("\n") == std::string::npos) {
      logMsg += "\n";
    }
    if (file_assist.writeFile(logPath, std::string(logMsg)).is_ok()) {
      return true;
    }
    return false;
  };

  if (getuid() != 0) {
    msg("run with sudo!");
    return 0;
  }

  if (backup.createBackup()) {
    msg("backup created success!");
  } else {
    msg("failed to create backup");
    return 0;
  }

  tools.gamingTools(result);
  auto results = [&](const LpBooster::Tools &tools,
                     auto LpBooster::Tools::Results::*flags) -> bool {
    return tools.*flags;
  };

  if (result.isCpuSuccess) {
    msg("cpu success!");
  }
  if (result.isGamingServicesSuccess) {
    msg("gaming services success!");
  }
  if (result.isServiceCreateSuccess) {
    msg("service created success!");
  }
  if (result.isSplitLockSuccess) {
    msg("split lock success!");
  }
  if (result.isGpuSuccess) {
    msg("gpu success!");
  }
  if (result.isTweaksSuccess) {
    msg("tweaks success!");
  }

  if (tools.optimizeServices('y')) {
    msg("optimize services success!");
  } else if (tools.optimizeServices('y') == std::nullopt) {
    msg("nothing to optimize!");
  } else {
    msg("optimize services failed!");
  }

  if (tools.optimizeDiskScheduler()) {
    msg("optimize disk scheduler success!");
  } else {
    msg("optimize disk scheduler failed!");
  }

  return 0;
}

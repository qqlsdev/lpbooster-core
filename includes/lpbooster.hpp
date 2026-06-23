#pragma once
#include "../utils/macros.hpp"
#include "../utils/tools.hpp"

#include <functional>
#include <optional>
#include <string>

using namespace Config;

namespace LpBooster {
class Tools {
private:
  FileAssist &file_assist;

  std::string trim(const std::string &str);
  void appendSysctl(const std::string &setting);
  bool applyTweaks();
  bool cpuPerformance();
  bool splitLock();
  bool gamingServices();
  bool createCpuService();
  std::optional<bool> gpuBoost();

public:
  explicit Tools(FileAssist &fileassist) : file_assist(fileassist) {}

  struct Results {
    bool isCpuSuccess;
    bool isTweaksSuccess;
    std::optional<bool> isGpuSuccess;
    bool isServiceCreateSuccess;
    bool isGamingServicesSuccess;
    bool isSplitLockSuccess;
  };

  void cleanSystem();
  std::optional<bool> optimizeServices(char skipBluetooth);
  bool optimizeDiskScheduler();
  void gamingTools(Results &tools_result);
};
} // namespace LpBooster

namespace Backup {
class BackupManager {
private:
  FileAssist &file_assist;

  const fs::path PARENT_DIR = fs::path(BACKUP_PATH).parent_path();

  static std::string trimTrailingNewline(std::string s);
  std::vector<std::string> listMatchingEntries(
      const std::string &dirPath,
      const std::function<bool(const std::string &)> &predicate) const;
  static bool isCpuDir(const std::string &name);
  static bool isCardDir(const std::string &name);
  bool isVirtualDisk(const std::string &name) const;
  std::vector<std::string> collectCpuGovernorPaths() const;
  std::vector<std::string> collectDrmGovernorPaths() const;
  std::vector<std::string> collectBlockSchedulerPaths() const;
  std::vector<std::string> collectAllPaths() const;
  void backupSysctlIfNeeded(FileAssist &file_assist) const;
  std::string buildBackupContent(const std::vector<std::string> &paths,
                                 FileAssist &file_assist);
  bool writeBackupFile(const std::string &content);

public:
  explicit BackupManager(FileAssist &_file_assist) : file_assist(_file_assist) {
    FileAssist file_assist;

    if (!fs::exists(PARENT_DIR)) {
      try {
        fs::create_directories(PARENT_DIR);
      } catch (const fs::filesystem_error &e) {
        return;
      }
    }

    if (!file_assist.createFile(BACKUP_PATH).is_ok()) {
      return;
    }
  }

  bool createBackup();
  bool restoreSysctl();
  bool restoreCpuGovernor();
  bool restoreGpuGovernor();
  bool restoreDiskScheduler();
  bool restoreSplitLock();
  bool MainRestoring();
  bool restoreAll();
};
} // namespace Backup

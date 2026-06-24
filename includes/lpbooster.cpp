#include "lpbooster.hpp"

#include "../utils/macros.hpp"

#include <INIReader.h>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;

double SystemMonitoring::getFreeRamInPercentages() {

  long long freePages = sysconf(_SC_AVPHYS_PAGES);
  long long freeBytes = freePages * Config::PAGE_SIZE;

  double freeRAM = static_cast<double>(freeBytes) / Config::GIGABYTE;

  return ((double)freeRAM * 100.0 / (double)SystemUtils::getTotalRAM());
}

double SystemMonitoring::getFreeRamInGigabytes() {

  long long freePages = sysconf(_SC_AVPHYS_PAGES);
  long long freeBytes = freePages * Config::PAGE_SIZE;

  return static_cast<double>(freeBytes) / Config::GIGABYTE;
}

std::string SystemMonitoring::getRamInText() {
  double free = SystemMonitoring::getFreeRamInGigabytes();
  double total = SystemUtils::getTotalRAM();

  if (free <= 0 || total <= 0)
    return std::string("Loading...");

  std::stringstream ss;
  ss << std::fixed << std::setprecision(2) << free << "GB / " << total << "GB";

  return ss.str();
}

namespace LpBooster {

std::string Tools::trim(const std::string &str) {
  const size_t first = str.find_first_not_of(" \t");
  if (std::string::npos == first)
    return "";
  const size_t last = str.find_last_not_of(" \t");
  return str.substr(first, (last - first + 1));
}

void Tools::appendSysctl(const std::string &setting) {
  std::string currContent;

  if (!fs::exists(Config::SYSCTL_CONF_PATH)) {
    if (!file_assist.createFile(Config::SYSCTL_CONF_PATH).is_ok()) {
    }
  }

  if (!file_assist.readFile(Config::SYSCTL_CONF_PATH, currContent).is_ok()) {
    return;
  }

  auto equalPos = setting.find('=');
  if (equalPos == std::string::npos)
    return;
  std::string targetKey = trim(setting.substr(0, equalPos));

  std::istringstream stream(currContent);
  std::string line;
  bool exists = false;

  while (std::getline(stream, line)) {
    line = trim(line);

    if (line.empty() || line[0] == '#' || line[0] == ';') {
      continue;
    }

    auto lineEqualPos = line.find('=');
    if (lineEqualPos != std::string::npos) {
      std::string currentKey = trim(line.substr(0, lineEqualPos));

      if (currentKey == targetKey) {
        exists = true;
        break;
      }
    }
  }

  if (exists) {
    return;
  }

  if (!file_assist.writeFile(Config::SYSCTL_CONF_PATH, setting).is_ok()) {
    return;
  }
}

void Tools::cleanSystem() {
  auto tryClean = [&](const std::string &binary, const std::string &cmd) {
    std::string binaryPath = SystemUtils::getCmdOutput("command -v " + binary);

    if (!binaryPath.empty()) {
      SystemUtils::runCommand(cmd);
    }
  };

  tryClean("pacman", "pacman -Qdtq | pacman -Rns - --noconfirm");
  tryClean("apt-get",
           "apt-get autoremove -y > /dev/null 2>&1 && apt-get clean");

  tryClean("dnf", "dnf autoremove -y");
  tryClean("flatpak", "flatpak uninstall --unused -y");
  tryClean("fc-cache", "fc-cache -r");

  if (const char *user = std::getenv("SUDO_USER")) {
    fs::path cacheDir = fs::path("/home") / user / ".cache";
    for (const auto &c : {"thumbnails", "fontconfig", "pip"}) {
      fs::path pCache = cacheDir / c;
      if (fs::exists(pCache)) {
        std::string cmd =
            std::format("sudo -u {} rm -rf {}", user, pCache.c_str());
        SystemUtils::runCommand(cmd);
      }
    }
  }

  SystemUtils::runCommand("journalctl --vacuum-size=50M");
}

std::optional<bool> Tools::optimizeServices(char skipBluetooth) {
  std::vector<std::string_view> toDisable;
  for (const auto &srv : Config::SERVICES_TO_DISABLE) {
    if (srv == "bluetooth.service" &&
        (skipBluetooth == 'y' || skipBluetooth == 'Y')) {
      continue;
    }
    toDisable.push_back(srv);
  }

  bool hdd, ssd;
  SystemUtils::detectDrives(hdd, ssd);
  if (hdd) {
    toDisable.push_back("tracker-miner-fs-3.service");
    if (!ssd)
      toDisable.push_back("fstrim.timer");
  }

  if (toDisable.empty()) {
    return std::nullopt;
  }

  std::string cmd = "systemctl disable --now";

  for (const auto &srv : toDisable) {
    std::string fullCmd = cmd + std::format(" {}", srv);
    SystemUtils::runCommand(fullCmd);
  }
  return true;
}

bool Tools::applyTweaks() {
  for (auto const &tweak : Config::VM_TWEAKS)
    appendSysctl(tweak);

  bool hdd, ssd;
  SystemUtils::detectDrives(hdd, ssd);
  if (ssd || SystemUtils::getTotalRAM() >= 8.0) {
    appendSysctl("vm.dirty_background_ratio=5");
    appendSysctl("vm.dirty_ratio=10");
  }

  if (system("sysctl -p /etc/sysctl.d/99-sysctl.conf > /dev/null 2>&1")) {
    return true;
  }

  return false;
}

bool Tools::cpuPerformance() {
  int fd_dir = open(Config::CPU_PATH.c_str(),
                    O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY);
  if (fd_dir < 0) {
    if (errno == ENOENT) {
      return false;
    } else {
      return false;
    }
    return true;
  }

  DIR *dir = fdopendir(fd_dir);

  if (!dir) {
    close(fd_dir);
  } else {
    dirent *entry;
    while ((entry = readdir(dir)) != nullptr) {
      std::string dName = entry->d_name;

      if (dName.starts_with("cpu") && dName.size() > 3 &&
          std::isdigit(dName[3])) {
        const std::string govPath =
            Config::CPU_PATH + dName + "/cpufreq/scaling_governor";

        if (!fs::exists(govPath)) {
          continue;
        }

        int fd_gov = open(govPath.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (fd_gov < 0) {
          continue;
        }

        std::vector<char> buffer(64);
        ssize_t bytesRead = read(fd_gov, buffer.data(), buffer.size() - 1);
        if (bytesRead < 0) {
          close(fd_gov);
          continue;
        }

        buffer[bytesRead] = '\0';
        std::string currGov = buffer.data();
        size_t lastCharPos = currGov.find_last_not_of(" \n\r\t");
        if (lastCharPos != std::string::npos)
          currGov.erase(lastCharPos + 1);
        else
          currGov.clear();
        close(fd_gov);

        if (currGov == Config::POWERSAVE) {
          if (!file_assist.writeFile(govPath, Config::PERFORMANCE).is_ok()) {
          }
        } else {
        }
      }
    }
    closedir(dir);
  }

  return true;
}

bool Tools::splitLock() {
  fs::path splPath = Config::SPLIT_LOCK_PATH;
  if (!fs::exists(splPath)) {
  } else {
    int fd_spl = open(splPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd_spl < 0) {
    } else {
      std::vector<char> splBuffer(8);
      ssize_t splBytesRead =
          read(fd_spl, splBuffer.data(), splBuffer.size() - 1);
      close(fd_spl);

      if (splBytesRead < 0) {
      } else if (splBytesRead > 0) {

        splBuffer[splBytesRead] = '\0';

        std::string currModeStr = splBuffer.data();
        textTrimmer(currModeStr);

        if (currModeStr != "0") {
          int fd_spl_write = open(splPath.c_str(), O_WRONLY | O_CLOEXEC);
          if (fd_spl_write < 0) {
          } else {
            const std::string disableMode = "0\n";
            ssize_t splBytesWritten =
                write(fd_spl_write, disableMode.c_str(), disableMode.length());
            if (splBytesWritten < 0 ||
                (size_t)splBytesWritten != disableMode.length()) {
            } else {
            }
            close(fd_spl_write);
          }
        } else {
        }
      }
    }
  }
  return true;
}

std::optional<bool> Tools::gpuBoost() {

  auto drmName = getDRM();

  if (!drmName.empty()) {
    const std::string fullPath = Config::DRM_PATH + drmName +
                                 "/device/power_dpm_force_performance_level";

    std::string readMode;

    if (!file_assist.readFile(fullPath, readMode).is_ok()) {
      return false;
    }

    if (!readMode.empty() && readMode.back() == '\n')
      readMode.pop_back();

    if (readMode == Config::HIGH) {
      return std::nullopt;
    }

    if (readMode == Config::AUTO || readMode == Config::LOW) {
      const std::string highMode = "high";
      if (!file_assist.writeFile(fullPath, highMode).is_ok()) {
      }
    } else {
    }
  }

  return true;
}

bool Tools::gamingServices() {
  for (const std::string &srv : {"ananicy.service", "gamemoded.service"}) {
    if (SystemUtils::runCommand("systemctl list-unit-files " + srv +
                                " | grep -q " + srv)) {
      SystemUtils::runCommand("systemctl enable --now " + srv);
    } else {
    }
  }
  return true;
}

bool Tools::createCpuService() {
  if (!fs::exists(Config::CPU_SERVICE_PATH)) {
    int fd_service =
        open(Config::CPU_SERVICE_PATH.c_str(),
             O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd_service < 0) {
    } else {
      const std::string service_content =
          "[Unit]\nDescription=Linux Performance "
          "Booster\nAfter=multi-user.target\n\n"
          "[Service]\nType=oneshot\nRemainAfterExit=yes\n"
          "ExecStart=/bin/sh -c 'echo performance | tee "
          "/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor'\n\n"
          "[Install]\nWantedBy=multi-user.target\n";

      ssize_t bytesWritten =
          write(fd_service, service_content.c_str(), service_content.length());
      if (bytesWritten < 0) {
      } else if ((size_t)bytesWritten != service_content.length()) {
      } else {
        SystemUtils::runCommand("systemctl daemon-reload && systemctl enable "
                                "lpbooster-cpu.service");
      }
      close(fd_service);
    }
  } else {
  }
  return true;
}

bool Tools::optimizeDiskScheduler() {
  int fd_block =
      open(Config::BLOCK_PATH.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

  if (fd_block < 0) {
    return false;
  }

  DIR *dir = fdopendir(fd_block);

  if (!dir) {
    close(fd_block);
    return false;
  }

  dirent *entry;

  while ((entry = readdir(dir)) != nullptr) {

    std::string dName = entry->d_name;

    if (dName.starts_with(".") || dName.starts_with(".."))
      continue;

    bool isVirtual = std::any_of(
        Config::VIRTUAL_DISKS.begin(), Config::VIRTUAL_DISKS.end(),
        [&](std::string_view vtd) { return dName.starts_with(vtd); });

    if (isVirtual)
      continue;

    const std::string schedPath =
        Config::BLOCK_PATH + dName + "/queue/scheduler";

    auto rotational =
        SystemUtils::isDriveRotational(Config::BLOCK_PATH + dName);

    if (!rotational.has_value()) {
      continue;
    }

    std::string currMode;
    std::string targetMode = rotational.value() ? Config::BFQ : Config::NONE;

    if (!file_assist.readFile(schedPath, currMode).is_ok()) {
      continue;
    }

    currMode = extractActiveScheduler(currMode);

    if (currMode.empty()) {
      continue;
    }

    if (currMode == targetMode) {
      continue;
    }

    if (file_assist.writeFile(schedPath, targetMode, 0).is_ok()) {
    } else {
      continue;
    }
  }
  closedir(dir);
  return true;
}

void Tools::gamingTools(Results &tools_result) {
  tools_result.isCpuSuccess = cpuPerformance();
  tools_result.isTweaksSuccess = applyTweaks();
  tools_result.isGpuSuccess = gpuBoost();
  tools_result.isServiceCreateSuccess = createCpuService();
  tools_result.isGamingServicesSuccess = gamingServices();
  tools_result.isSplitLockSuccess = splitLock();
  tools_result.AllSuccess = cpuPerformance() && applyTweaks() && gpuBoost() &&
                            createCpuService() && gamingServices() &&
                            splitLock();
}

} // namespace LpBooster

namespace Backup {

std::string Backup::BackupManager::trimTrailingNewline(std::string s) {
  if (!s.empty() && s.back() == '\n')
    s.pop_back();
  return s;
}

std::vector<std::string> Backup::BackupManager::listMatchingEntries(
    const std::string &dirPath,
    const std::function<bool(const std::string &)> &predicate) const {
  std::vector<std::string> result;
  DIR *dir = opendir(dirPath.c_str());
  if (!dir)
    return result;

  dirent *entry;
  while ((entry = readdir(dir)) != nullptr) {
    std::string dName = entry->d_name;
    if (dName == "." || dName == "..")
      continue;
    if (predicate(dName))
      result.push_back(dName);
  }
  closedir(dir);
  return result;
}

bool Backup::BackupManager::isCpuDir(const std::string &name) {
  return name.starts_with("cpu") && name.size() > 3 &&
         std::isdigit(static_cast<unsigned char>(name[3]));
}

bool Backup::BackupManager::isCardDir(const std::string &name) {
  return name.find("card") != std::string::npos && name.size() > 4 &&
         std::isdigit(static_cast<unsigned char>(name[4])) &&
         name.find('-') == std::string::npos;
}

bool Backup::BackupManager::isVirtualDisk(const std::string &name) const {
  return std::any_of(
      VIRTUAL_DISKS.begin(), VIRTUAL_DISKS.end(),
      [&](std::string_view vtd) { return name.starts_with(vtd); });
}

std::vector<std::string>
Backup::BackupManager::collectCpuGovernorPaths() const {
  std::vector<std::string> paths;
  for (auto &d : listMatchingEntries(CPU_PATH, isCpuDir))
    paths.push_back(CPU_PATH + d + "/cpufreq/scaling_governor");
  return paths;
}

std::vector<std::string>
Backup::BackupManager::collectDrmGovernorPaths() const {
  std::vector<std::string> paths;
  for (auto &d : listMatchingEntries(DRM_PATH, isCardDir))
    paths.push_back(DRM_PATH + d + "/device/power_dpm_force_performance_level");
  return paths;
}

std::vector<std::string>
Backup::BackupManager::collectBlockSchedulerPaths() const {
  std::vector<std::string> paths;
  auto entries =
      listMatchingEntries(BLOCK_PATH, [](const std::string &) { return true; });
  for (auto &d : entries) {
    if (isVirtualDisk(d))
      continue;
    paths.push_back(BLOCK_PATH + d + "/queue/scheduler");
  }
  return paths;
}

std::vector<std::string> Backup::BackupManager::collectAllPaths() const {
  std::vector<std::string> paths;

  if (fs::exists(SYSCTL_CONF_PATH))
    paths.push_back(SYSCTL_CONF_PATH);

  if (fs::exists(SPLIT_LOCK_PATH))
    paths.push_back(SPLIT_LOCK_PATH);

  auto append = [&](std::vector<std::string> &&extra) {
    paths.insert(paths.end(), extra.begin(), extra.end());
  };

  append(collectCpuGovernorPaths());
  append(collectDrmGovernorPaths());
  append(collectBlockSchedulerPaths());

  return paths;
}

void Backup::BackupManager::backupSysctlIfNeeded(
    FileAssist &file_assist) const {
  if (fs::exists(SYSCTL_CONF_PATH_BAK))
    return;

  if (!file_assist.createFile(SYSCTL_CONF_PATH_BAK).is_ok())
    return;

  std::string tmpStr;
  if (file_assist.readFile(SYSCTL_CONF_PATH, tmpStr).is_ok()) {
    file_assist.writeFile(SYSCTL_CONF_PATH_BAK, tmpStr).is_ok();
  }
}

std::string
Backup::BackupManager::buildBackupContent(const std::vector<std::string> &paths,
                                          FileAssist &file_assist) {
  std::string reader;

  for (auto const &path : paths) {
    if (path == SYSCTL_CONF_PATH) {
      backupSysctlIfNeeded(file_assist);
      continue;
    }

    std::string value;
    if (!file_assist.readFile(path, value).seek(0).is_ok()) {
      continue;
    }
    reader += path + "=" + value;
  }

  return reader;
}

bool Backup::BackupManager::writeBackupFile(const std::string &content) {
  int fd =
      open(BACKUP_PATH.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW | O_TRUNC);

  if (fd < 0) {
    return false;
  }

  size_t totalWritten = 0;
  const char *ptr = content.data();
  size_t len = content.length();

  while (totalWritten < len) {
    ssize_t written = write(fd, ptr + totalWritten, len - totalWritten);

    if (written < 0) {
      if (errno == EINTR)
        continue;
      close(fd);
      return false;
    }

    totalWritten += written;
  }

  if (fsync(fd) < 0) {
    close(fd);
    return false;
  }

  if (close(fd) < 0) {
    return false;
  }

  return true;
}

bool Backup::BackupManager::createBackup() {
  FileAssist file_assist;

  const auto paths = collectAllPaths();
  const std::string content = buildBackupContent(paths, file_assist);

  if (content.empty()) {
    return false;
  }

  return writeBackupFile(content);
}

bool Backup::BackupManager::restoreSysctl() {
  FileAssist file_assist;
  std::string savedBak;

  if (!fs::exists(SYSCTL_CONF_PATH_BAK) &&
      !file_assist.readFile(SYSCTL_CONF_PATH_BAK, savedBak).is_ok()) {
    return false;
  }

  if (!file_assist.writeFile(SYSCTL_CONF_PATH, savedBak, 0).is_ok()) {
    return false;
  }

  if (file_assist.removeFile(SYSCTL_CONF_PATH_BAK)) {
  }

  return true;
}

bool Backup::BackupManager::restoreCpuGovernor() {
  if (!fs::exists(CPU_PATH)) {
    return false;
  }

  FileAssist file_assist;
  std::vector<std::string> cpuPaths;

  for (auto &d : listMatchingEntries(CPU_PATH, isCpuDir))
    cpuPaths.push_back(CPU_PATH + d + "/cpufreq/scaling_governor");

  if (cpuPaths.empty()) {
    return false;
  }

  INIReader reader(BACKUP_PATH);
  bool isRestored = false;
  bool isCpuExist = false;

  for (auto const &cpu : cpuPaths) {
    if (!reader.HasValue("", cpu))
      continue;

    auto mode = reader.Get("", cpu, "none");
    if (mode == "none")
      continue;

    if (file_assist.writeFile(cpu, mode).is_ok()) {
      std::string cpuMode;
      if (file_assist.readFile(cpu, cpuMode).is_ok()) {
        cpuMode = trimTrailingNewline(cpuMode);
        if (cpuMode == mode) {
          isCpuExist = true;
          continue;
        }
        if (isCpuExist) {
        }
      }
      isRestored = true;
    } else if (errno == EACCES) {
      return false;
    } else {
    }
  }

  if (!isRestored) {
    if (errno == EACCES) {
    } else {
    }
    return false;
  }

  return true;
}

bool Backup::BackupManager::restoreGpuGovernor() {
  auto getNameDrm = getDRM();
  std::string pathToGovernor = Config::DRM_PATH + getNameDrm +
                               "/device/power_dpm_force_performance_level";

  INIReader reader(Config::BACKUP_PATH);

  if (!reader.HasValue("", pathToGovernor)) {
    return false;
  }

  auto getMode = reader.Get("", pathToGovernor, "none");

  if (getMode == "none") {
    return false;
  }

  std::string currMode;
  if (file_assist.readFile(pathToGovernor, currMode).is_ok()) {
    currMode = trimTrailingNewline(currMode);
    if (currMode == getMode) {
      return true;
    }
  }

  if (file_assist.writeFile(pathToGovernor, getMode).is_ok()) {
    return true;
  }

  return false;
}

bool Backup::BackupManager::restoreDiskScheduler() {

  if (!fs::exists(Config::BLOCK_PATH))
    return false;

  std::vector<std::string> schedPaths = collectBlockSchedulerPaths();
  if (schedPaths.empty())
    return false;

  std::string currMode;

  INIReader reader(Config::BACKUP_PATH);
  for (auto const &sp : schedPaths) {

    if (!file_assist.readFile(sp, currMode).is_ok())
      continue;

    currMode = extractActiveScheduler(currMode);

    if (!reader.HasValue("", sp))
      continue;

    std::string r = reader.Get("", sp, "unknown");

    if (r == "unknown")
      continue;

    r = extractActiveScheduler(r);

    if (r == currMode) {
      continue;
    }
    if (file_assist.writeFile(sp, r, 0).is_ok()) {
    }
  }
  return true;
}

bool Backup::BackupManager::restoreSplitLock() {
  if (!fs::exists(Config::BACKUP_PATH)) {
    return false;
  }

  INIReader reader(Config::BACKUP_PATH);
  if (!reader.HasValue("", Config::SPLIT_LOCK_PATH)) {
    return false;
  }
  auto getValue = reader.Get("", Config::SPLIT_LOCK_PATH, "none");
  if (getValue == "none") {
    return false;
  }

  std::string currValue;
  if (file_assist.readFile(Config::SPLIT_LOCK_PATH, currValue).is_ok()) {
    currValue = trimTrailingNewline(currValue);
    if (getValue == currValue) {
      return true;
    }
  }

  if (file_assist.writeFile(Config::SPLIT_LOCK_PATH, getValue).is_ok()) {
  }

  return true;
}

bool Backup::BackupManager::MainRestoring() {
  FileAssist file_assist;

  restoreGpuGovernor();
  restoreSplitLock();
  restoreDiskScheduler();

  return true;
}

bool Backup::BackupManager::restoreAll() {
  restoreSysctl();
  restoreCpuGovernor();
  MainRestoring();

  return true;
}

} // namespace Backup

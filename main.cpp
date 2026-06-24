#include "includes/lpbooster.hpp"
#include <chrono>
#include <filesystem>
#include <format>
#include <unistd.h>

int main() {

  FileAssist file_assist;
  Backup::BackupManager backup(file_assist);

  if (getuid() != 0) {
    return 0;
  }

  if (!backup.createBackup()) {
    return 0;
  }

  return 0;
}

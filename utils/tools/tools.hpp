#pragma once

#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <string>
#include <unistd.h>
#include <vector>

class FileStatus {
private:
  int fd = -1;
  bool ok = false;

public:
  FileStatus(int _fd, bool _ok) : fd(_fd), ok(_ok) {}

  ~FileStatus() {
    if (fd >= 0) {
      close(fd);
    }
  }

  FileStatus(const FileStatus &) = delete;
  FileStatus &operator=(const FileStatus &) = delete;

  FileStatus(FileStatus &&other) noexcept : fd(other.fd), ok(other.ok) {
    other.fd = -1;
    other.ok = false;
  }

  FileStatus &seek(off_t offset, int whence = SEEK_SET) {
    if (!ok || fd < 0)
      return *this;

    if (lseek(fd, offset, whence) < 0) {
      ok = false;
    }

    return *this;
  }

  bool is_ok() { return ok; }
};

class FileAssist {
public:
  [[nodiscard]] FileStatus readFile(const std::string &path,
                                    std::string &output);
  [[nodiscard]] FileStatus writeFile(const std::string &path, std::string input,
                                     int writeMode = O_APPEND);

  FileStatus createFile(const std::string &path);
  bool removeFile(const std::string &path);

  FileAssist() = default;
};

std::string textTrimmer(std::string &str);
std::string extractActiveScheduler(std::string str);
std::vector<std::string> getCPUPaths();
std::string getDRM();

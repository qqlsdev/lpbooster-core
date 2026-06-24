#include "tools.hpp"
#include "lpb-core/utils/macros.hpp"
#include <filesystem>

namespace fs = std::filesystem;

[[nodiscard]] FileStatus FileAssist::readFile(const std::string &path,
                                              std::string &output) {

  int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

  if (fd < 0) {
    return FileStatus(-1, false);
  }

  char buff[4096];
  ssize_t bytes_read;

  lseek(fd, 0, SEEK_SET);

  while ((bytes_read = read(fd, buff, sizeof(buff))) > 0) {
    output.append(buff, bytes_read);
  }

  if (bytes_read < 0) {
    return FileStatus(fd, false);
  }

  return FileStatus(fd, true);
}

[[nodiscard]] FileStatus FileAssist::writeFile(const std::string &path,
                                               std::string input,
                                               int writeMode) {
  int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW | writeMode);
  if (fd < 0) {
    if (errno == ENOENT) {
    }
    return FileStatus(errno, false);
  }

  if (input.find('\n') == std::string::npos)
    input += '\n';

  ssize_t write_bytes = write(fd, input.c_str(), input.length());

  if (write_bytes < 0) {
    return FileStatus(errno, false);
  }

  if ((size_t)write_bytes != input.length()) {
    return FileStatus(EIO, false);
  }

  return FileStatus(0, true);
}

FileStatus FileAssist::createFile(const std::string &path) {

  int fd = open(path.c_str(),
                O_WRONLY | O_NOFOLLOW | O_CLOEXEC | O_CREAT | O_TRUNC | O_EXCL,
                0666);

  if (fd < 0) {
    if (errno == EEXIST) {
      return FileStatus(-1, true);
    }
    return FileStatus(-1, false);
  }

  return FileStatus(fd, true);
}

bool FileAssist::removeFile(const std::string &path) {

  if (!fs::exists(path)) {
    return false;
  }

  if (!fs::remove(path)) {
    return false;
  }

  return true;
}

std::string textTrimmer(std::string &str) {
  const std::string whiteList = " \n\t\r";

  size_t end = str.find_last_not_of(whiteList);
  if (end != std::string::npos) {
    str.erase(end + 1);
  } else {
    str.clear();
    return "";
  }

  size_t begin = str.find_first_not_of(whiteList);
  if (begin != std::string::npos) {
    str.erase(0, begin);
  }

  return str;
}

std::string extractActiveScheduler(std::string str) {
  str = textTrimmer(str);
  size_t start = str.find('[');
  size_t end = str.find(']');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    return str.substr(start + 1, end - start - 1);
  }
  return str;
}

std::vector<std::string> getCPUPaths() {
  std::vector<std::string> governorPaths;

  if (!fs::exists(Config::CPU_PATH) || !fs::is_directory(Config::CPU_PATH)) {
    return governorPaths;
  }

  for (const auto &entry : fs::directory_iterator(Config::CPU_PATH)) {
    std::string dName = entry.path().filename().string();

    if (dName.starts_with("cpu") && dName.size() > 3 &&
        std::isdigit(static_cast<unsigned char>(dName[3]))) {

      std::string govPath =
          Config::CPU_PATH + dName + "/cpufreq/scaling_governor";

      if (fs::exists(govPath)) {
        governorPaths.push_back(govPath);
      }
    }
  }

  return governorPaths;
}

std::string getDRM() {

  if (!fs::exists(Config::DRM_PATH)) {
    return "";
  }

  int fd = open(Config::DRM_PATH.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);

  DIR *dir = fdopendir(fd);
  if (!dir)
    return "";

  dirent *entry;

  while ((entry = readdir(dir)) != nullptr) {
    std::string d_name = entry->d_name;
    if (d_name.find("card") != std::string::npos && d_name.size() > 4 &&
        isdigit(d_name[4]) && d_name.find('-') == std::string::npos) {
      return d_name;
    } else {
      continue;
    }
  }

  return "";
}

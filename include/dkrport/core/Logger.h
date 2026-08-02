#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace dkrport {

class Logger {
  public:
    bool Initialise(const std::filesystem::path& directory, std::string& error);
    void Info(const std::string& message);
    void Warning(const std::string& message);
    void Error(const std::string& message);
    const std::filesystem::path& CurrentLogPath() const;

  private:
    void Write(const char* level, const std::string& message);
    std::filesystem::path m_path;
    std::ofstream m_stream;
    mutable std::mutex m_mutex;
};

} // namespace dkrport

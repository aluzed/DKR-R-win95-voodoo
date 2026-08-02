#include "dkrport/core/Logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace dkrport {
namespace {
std::tm LocalTime(std::time_t value) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &value);
#else
    localtime_r(&value, &result);
#endif
    return result;
}

std::string Timestamp(const char* format) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    const std::tm local = LocalTime(time);
    std::ostringstream output;
    output << std::put_time(&local, format);
    return output.str();
}
}

bool Logger::Initialise(const std::filesystem::path& directory, std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        error = "Could not create log directory: " + ec.message();
        return false;
    }
    m_path = directory / ("dkr-port-" + Timestamp("%Y%m%d-%H%M%S") + ".log");
    m_stream.open(m_path, std::ios::out | std::ios::app);
    if (!m_stream) {
        error = "Could not create log file.";
        return false;
    }
    return true;
}

void Logger::Info(const std::string& message) { Write("INFO", message); }
void Logger::Warning(const std::string& message) { Write("WARN", message); }
void Logger::Error(const std::string& message) { Write("ERROR", message); }

const std::filesystem::path& Logger::CurrentLogPath() const { return m_path; }

void Logger::Write(const char* level, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const std::string line = Timestamp("%Y-%m-%d %H:%M:%S") + " [" + level + "] " + message;
    std::cout << line << std::endl;
    if (m_stream) {
        m_stream << line << '\n';
        m_stream.flush();
    }
}

} // namespace dkrport

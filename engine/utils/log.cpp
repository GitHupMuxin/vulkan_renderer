#include "engine/utils/log.h"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <cstdlib>

namespace engine::utils 
{

    Logger& Logger::Instance() 
    {
        static Logger instance;
        return instance;
    }

    Logger::Logger()  = default;
    Logger::~Logger() = default;

    void Logger::SetLogFile(const std::string& filePath) 
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fileStream_ = std::make_unique<std::ofstream>(filePath, std::ios::out | std::ios::app);
        if (fileStream_->is_open()) 
        {
            output_ = fileStream_.get();
        }
    }

    void Logger::SetLogLevel(LogLevel level) 
    {
        level_ = level;
    }

    void Logger::Debug(const std::string& message)   { Log(LogLevel::Debug,   message); }
    void Logger::Info(const std::string& message)    { Log(LogLevel::Info,    message); }
    void Logger::Warning(const std::string& message) { Log(LogLevel::Warning, message); }
    void Logger::Error(const std::string& message)   { Log(LogLevel::Error,   message); }
    void Logger::Fatal(const std::string& message) 
    {
        Log(LogLevel::Fatal, message);
        std::abort();
    }

    void Logger::Log(LogLevel level, const std::string& message) 
    {
        if (level < level_) return;

        static const char* levelNames[] = { "DEBUG", "INFO", "WARN", "ERROR", "FATAL" };

        auto now  = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::lock_guard<std::mutex> lock(mutex_);

        // 避免 put_time 污染全局 locale，用 thread_local buffer
        char timeBuffer[32];
        std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", std::localtime(&time));

        *output_ << "[" << timeBuffer << "." << std::setfill('0') << std::setw(3) << ms.count() << "] "
                << "[" << levelNames[static_cast<int>(level)] << "] "
                << message << std::endl;
    }

} // namespace engine::utils

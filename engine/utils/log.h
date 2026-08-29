#pragma once

#include <cassert>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

namespace engine::utils
{
    enum class LogLevel
    {
        Debug   = 0,
        Info    = 1,
        Warning = 2,
        Error   = 3,
        Fatal   = 4,
    };

    class Logger
    {
    public:
        static Logger& Instance();

        bool SetLogFile(const std::string& filePath, bool append = false);
        bool EnableConsoleOutput(bool createIfMissing = false);

        void SetLogLevel(LogLevel level);
        void SetFileLogLevel(LogLevel level);
        void SetConsoleLogLevel(LogLevel level);

        void Debug(const std::string& message);
        void Info(const std::string& message);
        void Warning(const std::string& message);
        void Error(const std::string& message);
        void Fatal(const std::string& message);

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

    private:
        Logger();
        ~Logger();

        void Log(LogLevel level, const std::string& message);

        LogLevel fileLevel_ = LogLevel::Debug;
        LogLevel consoleLevel_ = LogLevel::Info;
        bool consoleEnabled_ = false;
        bool consoleColorEnabled_ = false;
        std::mutex mutex_;
        std::unique_ptr<std::ofstream> fileStream_;
    };
} // namespace engine::utils

#define LOG_DEBUG(msg)   do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Debug(_os.str());   } while(0)
#define LOG_INFO(msg)    do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Info(_os.str());    } while(0)
#define LOG_WARN(msg)    do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Warning(_os.str()); } while(0)
#define LOG_ERROR(msg)   do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Error(_os.str());   } while(0)
#define LOG_FATAL(msg)   do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Fatal(_os.str());   } while(0)
#define SUCCESS_OR_LOG(success, msg) \
    do { if (!(success)) { LOG_FATAL(msg); assert(success); } } while(0)

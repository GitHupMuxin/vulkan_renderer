#pragma once

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <mutex>
#include <assert.h>

namespace engine::utils 
{

    enum class LogLevel {
        Debug   = 0,
        Info    = 1,
        Warning = 2,
        Error   = 3,
        Fatal   = 4,
    };

    class Logger 
    {
    public:
        static Logger&                  Instance();

        void                            SetLogFile(const std::string& filePath);
        void                            SetLogLevel(LogLevel level);

        void                            Debug(const std::string& message);
        void                            Info(const std::string& message);
        void                            Warning(const std::string& message);
        void                            Error(const std::string& message);
        void                            Fatal(const std::string& message);

        Logger(const Logger&)            = delete;
        Logger& operator=(const Logger&) = delete;

    private:
        Logger();
        ~Logger();

        void                            Log(LogLevel level, const std::string& message);

        LogLevel                        level_     = LogLevel::Debug;
        std::mutex                      mutex_;
        std::unique_ptr<std::ofstream>  fileStream_;
        std::ostream*                   output_    = &std::cerr;  // 默认 stderr
    };

} // namespace engine::utils

// ---- 宏：宏调用 Logger 的函数 ----
#define LOG_DEBUG(msg)   do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Debug(_os.str());   } while(0)
#define LOG_INFO(msg)    do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Info(_os.str());    } while(0)
#define LOG_WARN(msg)    do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Warning(_os.str()); } while(0)
#define LOG_ERROR(msg)   do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Error(_os.str());   } while(0)
#define LOG_FATAL(msg)   do { std::ostringstream _os; _os << msg; engine::utils::Logger::Instance().Fatal(_os.str());   } while(0)
#define SUCCESS_OR_LOG(success, msg) \
    do { if (!(success)) { LOG_FATAL(msg); assert(success); } } while(0) 
    
    
    
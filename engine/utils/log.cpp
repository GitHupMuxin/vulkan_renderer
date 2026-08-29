#include "engine/utils/log.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace
{
    constexpr std::array<const char*, 5> kLevelNames = {
        "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
    };

    constexpr std::array<const char*, 5> kLevelColors = {
        "\x1b[90m",   // Debug: 灰
        "\x1b[32m",   // Info: 绿
        "\x1b[33m",   // Warning: 黄
        "\x1b[31m",   // Error: 红
        "\x1b[1;31m"  // Fatal: 加粗红
    };

    constexpr const char* kResetColor = "\x1b[0m";

    bool PrepareConsole(bool createIfMissing, bool& colorEnabled)
    {
#if defined(_WIN32)
        if (GetConsoleWindow() == nullptr)
        {
            if (!AttachConsole(ATTACH_PARENT_PROCESS) && (!createIfMissing || !AllocConsole()))
            {
                return false;
            }
        }

        HANDLE outputHandle = GetStdHandle(STD_ERROR_HANDLE);
        if (outputHandle == nullptr || outputHandle == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        DWORD consoleMode = 0;
        if (!GetConsoleMode(outputHandle, &consoleMode))
        {
            return false;
        }

        if (colorEnabled
            && !SetConsoleMode(outputHandle, consoleMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING))
        {
            colorEnabled = false;
        }

        SetConsoleOutputCP(CP_UTF8);
        return true;
#else
        (void)createIfMissing;
        (void)colorEnabled;
        return isatty(fileno(stderr)) != 0;
#endif
    }

    void WriteConsoleText(const std::string& text)
    {
#if defined(_WIN32)
        HANDLE outputHandle = GetStdHandle(STD_ERROR_HANDLE);
        DWORD written = 0;
        WriteFile(outputHandle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
#else
        std::fwrite(text.data(), sizeof(char), text.size(), stderr);
        std::fflush(stderr);
#endif
    }
}

namespace engine::utils
{
    Logger& Logger::Instance()
    {
        static Logger instance;
        return instance;
    }

    Logger::Logger() = default;
    Logger::~Logger() = default;

    bool Logger::SetLogFile(const std::string& filePath, bool append)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto openMode = std::ios::out | (append ? std::ios::app : std::ios::trunc);
        auto fileStream = std::make_unique<std::ofstream>(filePath, openMode);
        if (!fileStream->is_open())
        {
            return false;
        }

        fileStream_ = std::move(fileStream);
        return true;
    }

    bool Logger::EnableConsoleOutput(bool createIfMissing)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        consoleColorEnabled_ = std::getenv("NO_COLOR") == nullptr;
        consoleEnabled_ = PrepareConsole(createIfMissing, consoleColorEnabled_);
        return consoleEnabled_;
    }

    void Logger::SetLogLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fileLevel_ = level;
        consoleLevel_ = level;
    }

    void Logger::SetFileLogLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fileLevel_ = level;
    }

    void Logger::SetConsoleLogLevel(LogLevel level)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        consoleLevel_ = level;
    }

    void Logger::Debug(const std::string& message)   { Log(LogLevel::Debug, message); }
    void Logger::Info(const std::string& message)    { Log(LogLevel::Info, message); }
    void Logger::Warning(const std::string& message) { Log(LogLevel::Warning, message); }
    void Logger::Error(const std::string& message)   { Log(LogLevel::Error, message); }

    void Logger::Fatal(const std::string& message)
    {
        Log(LogLevel::Fatal, message);
        std::abort();
    }

    void Logger::Log(LogLevel level, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if ((!fileStream_ || level < fileLevel_) && (!consoleEnabled_ || level < consoleLevel_))
        {
            return;
        }

        const auto now = std::chrono::system_clock::now();
        const auto time = std::chrono::system_clock::to_time_t(now);
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        std::tm localTime{};
#if defined(_WIN32)
        localtime_s(&localTime, &time);
#else
        localtime_r(&time, &localTime);
#endif

        char timeBuffer[16]{};
        std::strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &localTime);

        const size_t levelIndex = static_cast<size_t>(level);
        std::ostringstream prefix;
        prefix << "[" << timeBuffer << "." << std::setfill('0') << std::setw(3)
               << milliseconds.count() << "] ";

        const std::string prefixText = prefix.str();
        const std::string levelText = "[" + std::string(kLevelNames[levelIndex]) + "]";

        if (fileStream_ && level >= fileLevel_)
        {
            *fileStream_ << prefixText << levelText << " " << message << '\n';
            fileStream_->flush();
        }

        if (consoleEnabled_ && level >= consoleLevel_)
        {
            const std::string displayedLevel = consoleColorEnabled_
                ? kLevelColors[levelIndex] + levelText + kResetColor
                : levelText;
            const std::string consoleText = prefixText + displayedLevel + " " + message + "\n";
            WriteConsoleText(consoleText);
        }
    }
} // namespace engine::utils

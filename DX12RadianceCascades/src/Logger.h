/*

    THIS FILE WAS INITIALLY GENERATED BY AI.

*/

#pragma once

#include <string>
#include <format>
#include <iostream>
#include <source_location>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <windows.h>

namespace Logging
{
    enum class LogLevel
    {
        Error,      // Critical errors that prevent the application from continuing
        Warning,    // Non-critical errors or potential issues
        Info,       // General information about application state
        Debug       // Detailed information for debugging purposes
    };

#if defined(_DEBUG)
    static const LogLevel s_DefaultLogLevel = LogLevel::Debug;
#else
    static const LogLevel s_DefaultLogLevel = LogLevel::Info;
#endif

    class Logger
    {
    public:
        static Logger& Get()
        {
            static Logger instance;
            return instance;
        }

        // Initialize the logger with optional file output
        void Initialize(bool consoleOutput = true, const std::wstring& logFilePath = L"")
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_consoleOutput = consoleOutput;

            if (!logFilePath.empty())
            {
                m_fileOutput = true;
                m_logFile.open(logFilePath, std::ios::out | std::ios::trunc);

                if (!m_logFile.is_open())
                {
                    OutputToConsole(LogLevel::Error, L"Failed to open log file: " + logFilePath);
                    m_fileOutput = false;
                }
            }

            // Create a console if needed and the app doesn't already have one
            if (m_consoleOutput && !m_consoleCreated)
            {
                if (AllocConsole())
                {
                    m_consoleCreated = true;
                    FILE* dummy;
                    freopen_s(&dummy, "CONOUT$", "w", stdout);
                }
            }

            m_initialized = true;
        }

        void SetLogLevel(LogLevel level)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_currentLevel = level;
        }

        void Shutdown()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_fileOutput && m_logFile.is_open())
            {
                m_logFile.close();
            }

            if (m_consoleCreated)
            {
                FreeConsole();
                m_consoleCreated = false;
            }

            m_initialized = false;
        }

        // Base log methods without formatting arguments
        void ErrorBase(const std::wstring& message, const std::source_location& location = std::source_location::current())
        {
            LogDirect(LogLevel::Error, message, location);
        }

        void WarningBase(const std::wstring& message, const std::source_location& location = std::source_location::current())
        {
            LogDirect(LogLevel::Warning, message, location);
        }

        void InfoBase(const std::wstring& message, const std::source_location& location = std::source_location::current())
        {
            LogDirect(LogLevel::Info, message, location);
        }

        void DebugBase(const std::wstring& message, const std::source_location& location = std::source_location::current())
        {
            LogDirect(LogLevel::Debug, message, location);
        }

        // Formatted log methods
        template<typename... Args>
        void Error(const std::source_location& location, std::wstring_view format, Args... args)
        {
            LogFormatted(location, LogLevel::Error, format, args...);
        }

        template<typename... Args>
        void Warning(const std::source_location& location, std::wstring_view format, Args... args)
        {
            LogFormatted(location, LogLevel::Warning, format, args...);
        }

        template<typename... Args>
        void Info(const std::source_location& location, std::wstring_view format, Args... args)
        {
            LogFormatted(location, LogLevel::Info, format, args...);
        }

        template<typename... Args>
        void Debug(const std::source_location& location, std::wstring_view format, Args... args)
        {
            LogFormatted(location, LogLevel::Debug, format, args...);
        }

    private:
        Logger() : m_currentLevel(s_DefaultLogLevel), m_initialized(false),
            m_consoleOutput(true), m_fileOutput(false), m_consoleCreated(false) {
        }
        ~Logger() { Shutdown(); }

        // Delete copy and move operations
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        // Log a direct message without formatting
        void LogDirect(LogLevel level, const std::wstring& message, const std::source_location& location)
        {
            if (!m_initialized || level > m_currentLevel)
                return;

            std::lock_guard<std::mutex> lock(m_mutex);

            // Format with timestamp, level, file, line
            auto now = std::chrono::system_clock::now();
            auto time = std::chrono::system_clock::to_time_t(now);
            std::tm tm;
            localtime_s(&tm, &time);

            std::wstring formattedTime = std::format(L"{:04d}-{:02d}-{:02d} {:02d}:{:02d}:{:02d}",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);

            std::wstring levelString;
            switch (level)
            {
            case LogLevel::Error:   levelString = L"ERROR"; break;
            case LogLevel::Warning: levelString = L"WARNING"; break;
            case LogLevel::Info:    levelString = L"INFO"; break;
            case LogLevel::Debug:   levelString = L"DEBUG"; break;
            }

            // Convert file path to wstring
            const auto filePath = std::filesystem::path(location.file_name()).filename().string();
            std::wstring fileStr(filePath.begin(), filePath.end());

            std::wstring fullMessage = std::format(L"[{}] [{}] [{}:{}] {}\n",
                formattedTime, levelString, fileStr, location.line(), message);

            if (m_consoleOutput)
            {
                OutputToConsole(level, fullMessage);
            }

            if (m_fileOutput && m_logFile.is_open())
            {
                m_logFile << std::wstring_view(fullMessage).data();
                m_logFile.flush();
            }

            // Always output to the debugger if attached
            OutputDebugStringW(fullMessage.c_str());
        }

        template<typename... Args>
        void LogFormatted(const std::source_location& location, LogLevel level, std::wstring_view format, Args... args)
        {
            if (!m_initialized || level > m_currentLevel)
                return;

            try
            {
                // Create copies of arguments to ensure they remain valid
                std::wstring formattedMessage = std::vformat(format, std::make_wformat_args(args...));
                LogDirect(level, formattedMessage, location);
            }
            catch (const std::exception& e)
            {
                std::wstring errorMsg = L"Formatting error: ";
                errorMsg += std::wstring(e.what(), e.what() + strlen(e.what()));
                LogDirect(LogLevel::Error, errorMsg, std::source_location::current());
            }
        }

        void OutputToConsole(LogLevel level, const std::wstring& message)
        {
            HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
            WORD originalAttrs = 0;
            CONSOLE_SCREEN_BUFFER_INFO csbi;

            if (GetConsoleScreenBufferInfo(hConsole, &csbi))
            {
                originalAttrs = csbi.wAttributes;
            }

            // Set color based on log level
            switch (level)
            {
            case LogLevel::Error:
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_INTENSITY);
                break;
            case LogLevel::Warning:
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
                break;
            case LogLevel::Info:
                SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
                break;
            case LogLevel::Debug:
                SetConsoleTextAttribute(hConsole, FOREGROUND_GREEN | FOREGROUND_BLUE);
                break;
            }

            std::wcout << message;

            // Reset color
            SetConsoleTextAttribute(hConsole, originalAttrs);
        }

        std::mutex m_mutex;
        LogLevel m_currentLevel;
        bool m_initialized;
        bool m_consoleOutput;
        bool m_fileOutput;
        bool m_consoleCreated;
        std::wofstream m_logFile;
    };

    // Convenience functions for direct messages
    inline void ErrorDirect(const std::wstring& message)
    {
        Logger::Get().ErrorBase(message);
    }

    inline void WarningDirect(const std::wstring& message)
    {
        Logger::Get().WarningBase(message);
    }

    inline void InfoDirect(const std::wstring& message)
    {
        Logger::Get().InfoBase(message);
    }

    inline void DebugDirect(const std::wstring& message)
    {
        Logger::Get().DebugBase(message);
    }

    // Convenience functions for formatted messages
    template<typename... Args>
    inline void Error(const std::source_location& source, std::wstring_view format, Args... args)
    {
        Logger::Get().Error(source, format, args...);
    }

    template<typename... Args>
    inline void Warning(const std::source_location& source, std::wstring_view format, Args... args)
    {
        Logger::Get().Warning(source, format, args...);
    }

    template<typename... Args>
    inline void Info(const std::source_location& source, std::wstring_view format, Args... args)
    {
        Logger::Get().Info(source, format, args...);
    }

    template<typename... Args>
    inline void Debug(const std::source_location& source, std::wstring_view format, Args... args)
    {
        Logger::Get().Debug(source, format, args...);
    }

    // Initialize the logging system
    inline void Initialize(bool consoleOutput = true, const std::wstring& logFilePath = L"")
    {
        Logger::Get().Initialize(consoleOutput, logFilePath);
    }

    // Set the current log level
    inline void SetLogLevel(LogLevel level)
    {
        Logger::Get().SetLogLevel(level);
    }

    // Shutdown the logging system
    inline void Shutdown()
    {
        Logger::Get().Shutdown();
    }
}

// Macros for easier usage
#define LOG_ERROR(format, ...)    Logging::Error(std::source_location::current(), format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...)  Logging::Warning(std::source_location::current(), format, ##__VA_ARGS__)
#define LOG_INFO(format, ...)     Logging::Info(std::source_location::current(), format, ##__VA_ARGS__)
#define LOG_DEBUG(format, ...)    Logging::Debug(std::source_location::current(), format, ##__VA_ARGS__)

// Direct message macros
#define LOG_ERROR_DIRECT(message)   Logging::ErrorDirect(message)
#define LOG_WARNING_DIRECT(message) Logging::WarningDirect(message)
#define LOG_INFO_DIRECT(message)    Logging::InfoDirect(message)
#define LOG_DEBUG_DIRECT(message)   Logging::DebugDirect(message)
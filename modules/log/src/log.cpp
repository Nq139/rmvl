/**
 * @file log.cpp
 * @author Nq139 (1585062440@qq.com)
 * @brief 同步日志功能实现
 * @version 1.0
 * @date 2026-08-07
 *
 * @copyright Copyright 2026 (c), Nq139
 *
 */

#include "rmvl/log/log.hpp"

#include <ctime>
#include <iostream>
#include <regex>

namespace rm {

namespace {

std::tm localTime(std::time_t time) {
    std::tm result{};
#ifdef _WIN32
    localtime_s(&result, &time);
#else
    localtime_r(&time, &result);
#endif
    return result;
}

std::string dateOf(std::chrono::system_clock::time_point time) {
    const auto tm = localTime(std::chrono::system_clock::to_time_t(time));
    return fmt::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

const char *levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

bool parseLogDate(const std::string &name, std::tm &date) {
    static const std::regex pattern(R"(^([0-9]{4})-([0-9]{2})-([0-9]{2})\.log$)");
    std::smatch match;
    if (!std::regex_match(name, match, pattern))
        return false;

    date = {};
    date.tm_year = std::stoi(match[1].str()) - 1900;
    date.tm_mon = std::stoi(match[2].str()) - 1;
    date.tm_mday = std::stoi(match[3].str());
    date.tm_hour = 12;
    const auto year = date.tm_year;
    const auto month = date.tm_mon;
    const auto day = date.tm_mday;
    const auto normalized = std::mktime(&date);
    if (normalized == static_cast<std::time_t>(-1))
        return false;
    const auto verified = localTime(normalized);
    return verified.tm_year == year && verified.tm_mon == month && verified.tm_mday == day;
}

} // namespace

std::string formatLogRecord(const LogRecord &record) {
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(record.timestamp.time_since_epoch()) % 1000;
    const auto tm = localTime(std::chrono::system_clock::to_time_t(record.timestamp));
    return fmt::format("[{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:03}] [{}] {}",
                       tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                       milliseconds.count(), levelName(record.level), record.text);
}

ConsoleSink::ConsoleSink() : _stream(std::cout) {}

bool ConsoleSink::write(const LogRecord &record) {
    std::lock_guard<std::mutex> lock(_mtx);
    _stream << formatLogRecord(record) << '\n';
    _stream.flush();
    return static_cast<bool>(_stream);
}

FileSink::FileSink(std::filesystem::path directory, std::size_t retention_days)
    : _directory(std::move(directory)), _retention_days(retention_days) {
    const auto now = std::chrono::system_clock::now();
    _current_date = dateOf(now);
    _ready = cleanupExpired(now) && openFor(_current_date);
}

bool FileSink::write(const LogRecord &record) {
    std::lock_guard<std::mutex> lock(_mtx);
    const auto date = dateOf(record.timestamp);
    if (!_ready || date != _current_date) {
        _stream.close();
        _current_date = date;
        _ready = cleanupExpired(record.timestamp) && openFor(_current_date);
    }
    if (!_ready)
        return false;

    _stream << formatLogRecord(record) << '\n';
    _stream.flush();
    return static_cast<bool>(_stream);
}

bool FileSink::cleanupExpired(std::chrono::system_clock::time_point reference_time) {
    std::error_code error;
    std::filesystem::create_directories(_directory, error);
    if (error)
        return false;

    const auto cutoff = dateOf(reference_time - std::chrono::hours(24 * _retention_days));
    for (std::filesystem::directory_iterator it(_directory, error), end; !error && it != end; it.increment(error)) {
        const auto status = std::filesystem::symlink_status(it->path(), error);
        if (error)
            return false;
        if (!std::filesystem::is_regular_file(status))
            continue;

        std::tm date{};
        const auto name = it->path().filename().string();
        if (!parseLogDate(name, date))
            continue;
        const auto file_date = fmt::format("{:04}-{:02}-{:02}", date.tm_year + 1900, date.tm_mon + 1, date.tm_mday);
        if (file_date < cutoff) {
            std::filesystem::remove(it->path(), error);
            if (error)
                return false;
        }
    }
    return !error;
}

bool FileSink::openFor(const std::string &date) {
    _stream.clear();
    _stream.open(_directory / (date + ".log"), std::ios::out | std::ios::app);
    return _stream.is_open();
}

bool Logger::addSink(std::unique_ptr<Sink> sink) {
    if (!sink)
        return false;
    std::lock_guard<std::mutex> lock(_mtx);
    _sinks.push_back(std::move(sink));
    return true;
}

void Logger::clearSinks() {
    std::lock_guard<std::mutex> lock(_mtx);
    _sinks.clear();
}

bool Logger::write(const LogRecord &record) {
    std::lock_guard<std::mutex> lock(_mtx);
    bool success = true;
    for (const auto &sink : _sinks)
        success = sink->write(record) && success;
    return success;
}

Logger logger;

} // namespace rm

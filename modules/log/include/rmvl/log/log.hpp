/**
 * @file log.hpp
 * @author Nq139 (1585062440@qq.com)
 * @brief 同步日志功能
 * @version 1.0
 * @date 2026-08-07
 *
 * @copyright Copyright 2026 (c), Nq139
 *
 */

#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include "rmvl/core/rmvldef.hpp"

namespace rm {

//! @addtogroup log
//! @{

//! 日志等级
enum class LogLevel : uint8_t {
    Debug,   //!< 调试信息
    Info,    //!< 普通信息
    Warning, //!< 警告信息
    Error,   //!< 错误信息
};

//! 一条完整的日志记录
struct RMVL_EXPORTS LogRecord {
    std::chrono::system_clock::time_point timestamp; //!< 记录生成时间
    LogLevel level;                                  //!< 日志等级
    std::string text;                                //!< 已格式化的日志正文
};

/**
 * @brief 将日志记录转换为统一输出文本
 *
 * @param[in] record 日志记录
 * @return 格式为 `[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message` 的文本
 */
RMVL_EXPORTS std::string formatLogRecord(const LogRecord &record);

//! 日志输出后端基类
class RMVL_EXPORTS Sink {
public:
    virtual ~Sink() = default;

    /**
     * @brief 写入一条日志记录
     *
     * @param[in] record 日志记录
     * @return 是否写入成功
     */
    virtual bool write(const LogRecord &record) = 0;
};

//! 输出至标准输出流的日志后端
class RMVL_EXPORTS ConsoleSink final : public Sink {
public:
    //! 构造输出至标准输出流的日志后端
    ConsoleSink();

    /**
     * @brief 构造输出至指定流的日志后端
     *
     * @param[in] stream 输出流
     */
    explicit ConsoleSink(std::ostream &stream) : _stream(stream) {}

    bool write(const LogRecord &record) override;

private:
    std::ostream &_stream;
    std::mutex _mtx;
};

//! 按自然日期保存文件的日志后端
class RMVL_EXPORTS FileSink final : public Sink {
public:
    /**
     * @brief 构造文件日志后端
     *
     * @param[in] directory 日志目录
     * @param[in] retention_days 保留自然日期数，超出此范围的 `YYYY-MM-DD.log` 文件会被删除
     */
    explicit FileSink(std::filesystem::path directory, std::size_t retention_days = 7);

    bool write(const LogRecord &record) override;

private:
    bool cleanupExpired(std::chrono::system_clock::time_point reference_time);
    bool openFor(const std::string &date);

    std::filesystem::path _directory;
    std::size_t _retention_days;
    std::string _current_date;
    std::ofstream _stream;
    bool _ready{};
    std::mutex _mtx;
};

//! 同步多后端日志记录器
class RMVL_EXPORTS Logger {
public:
    Logger() = default;

    //! 添加一个由 Logger 独占管理的日志后端
    bool addSink(std::unique_ptr<Sink> sink);

    //! 移除并销毁所有日志后端
    void clearSinks();

    /**
     * @brief 写入已生成的日志记录
     * @note 每个已配置 Sink 都会恰好调用一次，即使其他 Sink 写入失败。
     */
    bool write(const LogRecord &record);

    template <typename... Args>
    bool debug(fmt::format_string<Args...> format, Args &&...args) {
        return log(LogLevel::Debug, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    bool info(fmt::format_string<Args...> format, Args &&...args) {
        return log(LogLevel::Info, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    bool warning(fmt::format_string<Args...> format, Args &&...args) {
        return log(LogLevel::Warning, format, std::forward<Args>(args)...);
    }

    template <typename... Args>
    bool error(fmt::format_string<Args...> format, Args &&...args) {
        return log(LogLevel::Error, format, std::forward<Args>(args)...);
    }

private:
    template <typename... Args>
    bool log(LogLevel level, fmt::format_string<Args...> format, Args &&...args) {
        LogRecord record{std::chrono::system_clock::now(), level, fmt::format(format, std::forward<Args>(args)...)};
        return write(record);
    }

    std::vector<std::unique_ptr<Sink>> _sinks;
    std::mutex _mtx;
};

//! RMVL 全局日志记录器
extern RMVL_EXPORTS Logger logger;

//! @} log

} // namespace rm

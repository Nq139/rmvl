/**
 * @file log.cpp
 * @author Nq139 (1585062440@qq.com)
 * @brief 基于 IO 模块的日志后端实现
 * @version 1.0
 * @date 2026-08-07
 *
 * @copyright Copyright 2026 (c), Nq139
 *
 */

#include "rmvl/io/log.hpp"

namespace rm {

bool SocketSink::write(const LogRecord &record) {
    std::lock_guard<std::mutex> lock(_mtx);
    return _socket.write(formatLogRecord(record));
}

bool SerialSink::write(const LogRecord &record) {
    std::lock_guard<std::mutex> lock(_mtx);
    return _serial.write(formatLogRecord(record));
}

} // namespace rm

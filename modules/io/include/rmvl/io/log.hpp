/**
 * @file log.hpp
 * @author Nq139 (1585062440@qq.com)
 * @brief 基于 IO 模块的日志后端
 * @version 1.0
 * @date 2026-08-07
 *
 * @copyright Copyright 2026 (c), Nq139
 *
 */

#pragma once

#include <mutex>

#include "rmvl/log/log.hpp"

#include "serial.hpp"
#include "socket.hpp"

namespace rm {

//! @addtogroup io
//! @{

//! 基于已连接流式 Socket 的日志后端
class RMVL_EXPORTS SocketSink final : public Sink {
public:
    /**
     * @brief 绑定一个已经建立连接的流式 Socket
     * @note 调用方必须保证 SocketSink 生命周期内 socket 有效。
     */
    explicit SocketSink(StreamSocket &socket) : _socket(socket) {}

    bool write(const LogRecord &record) override;

private:
    StreamSocket &_socket;
    std::mutex _mtx;
};

//! 基于现有串口的日志后端
class RMVL_EXPORTS SerialSink final : public Sink {
public:
    /**
     * @brief 绑定一个已打开的串口
     * @note 调用方必须保证 SerialSink 生命周期内 serial 有效。
     */
    explicit SerialSink(SerialPort &serial) : _serial(serial) {}

    bool write(const LogRecord &record) override;

private:
    SerialPort &_serial;
    std::mutex _mtx;
};

//! @} io

} // namespace rm

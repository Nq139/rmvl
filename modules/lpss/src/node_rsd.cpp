/**
 * @file node_rsd.cpp
 * @author zhaoxi (535394140@qq.com)
 * @brief RMVL 服务发现协议实现
 * @version 1.0
 * @date 2025-11-24
 *
 * @copyright Copyright 2025 (c), zhaoxi
 *
 */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif

#include <cstring>

#include "rmvl/lpss/details/node_rsd.hpp"

namespace rm::lpss {

namespace {

constexpr std::size_t LOCATOR_WIRE_SIZE = sizeof(uint16_t) + 4;

void write_locator(char *data, const Locator &locator) noexcept {
    const uint16_t net_port = htons(locator.port);
    ::memcpy(data, &net_port, sizeof(net_port));
    ::memcpy(data + sizeof(net_port), locator.addr.data(), locator.addr.size());
}

Locator read_locator(const char *data) noexcept {
    Locator locator{};
    uint16_t net_port{};
    ::memcpy(&net_port, data, sizeof(net_port));
    locator.port = ntohs(net_port);
    ::memcpy(locator.addr.data(), data + sizeof(net_port), locator.addr.size());
    return locator;
}

} // namespace

NegotiatedProtocols negotiateProtocols(const RNDPMessage &peer) noexcept {
    if (peer.ndp_version != 2)
        return {};
    // EDP 和 MTP 分别协商，当前使用 EDP 版本 1 和 MTP 版本 2
    return {static_cast<uint8_t>(peer.edp_version >= 1 ? 1 : 0),
            static_cast<uint8_t>(peer.mtp_version >= 2 ? 2 : 0)};
}

std::string RNDPMessage::serialize() const noexcept {
    if (locators.size() > UINT8_MAX || name.size() > UINT8_MAX)
        return {};
    constexpr size_t header_size = RNDP_HEADER_SIZE + 4;
    const auto num = static_cast<uint8_t>(locators.size());
    const auto name_size = static_cast<uint8_t>(name.size());
    std::string wire(header_size + num * LOCATOR_WIRE_SIZE + 1 + name_size, '\0');
    // RNDP 头部
    ::memcpy(wire.data(), "ND02", 4);
    ::memcpy(wire.data() + 4, &guid.full, sizeof(guid.full));
    wire[12] = 'v';
    wire[13] = static_cast<char>(ndp_version);
    wire[14] = static_cast<char>(edp_version);
    wire[15] = static_cast<char>(mtp_version);
    wire[16] = static_cast<char>(num);
    wire[17] = static_cast<char>(heartbeat_timeout);

    // RNDP 数据
    size_t offset = header_size;
    for (const auto &locator : locators) {
        write_locator(wire.data() + offset, locator);
        offset += LOCATOR_WIRE_SIZE;
    }
    wire[offset++] = static_cast<char>(name_size);
    ::memcpy(wire.data() + offset, name.data(), name_size);
    return wire;
}

std::optional<RNDPMessage> RNDPMessage::deserialize(std::string_view wire) noexcept {
    constexpr size_t header_size = RNDP_HEADER_SIZE + 4;
    if (wire.size() < header_size + 1 || wire.substr(0, 4) != "ND02" ||
        wire[12] != 'v' || static_cast<uint8_t>(wire[13]) != 2)
        return std::nullopt;
    const auto count = static_cast<uint8_t>(wire[16]);
    const size_t name_pos = header_size + size_t(count) * LOCATOR_WIRE_SIZE;
    if (name_pos >= wire.size())
        return std::nullopt;
    const auto name_len = static_cast<uint8_t>(wire[name_pos]);
    if (name_pos + 1 + name_len != wire.size())
        return std::nullopt;

    RNDPMessage result{};
    ::memcpy(&result.guid.full, wire.data() + 4, 8);
    if (!result.guid.full)
        return std::nullopt;
    result.ndp_version = static_cast<uint8_t>(wire[13]);
    result.edp_version = static_cast<uint8_t>(wire[14]);
    result.mtp_version = static_cast<uint8_t>(wire[15]);
    result.heartbeat_timeout = static_cast<uint8_t>(wire[17]);
    if (!result.heartbeat_timeout)
        return std::nullopt;
    result.locators.reserve(count);
    for (size_t i = 0; i < count; ++i)
        result.locators.emplace_back(read_locator(wire.data() + header_size + i * LOCATOR_WIRE_SIZE));
    result.name.assign(wire.data() + name_pos + 1, name_len);
    return result;
}

std::string REDPMessage::serialize() const noexcept {
    // 数据准备
    const uint8_t topic_size = static_cast<uint8_t>(topic.length());
    const uint8_t msgtype_size = static_cast<uint8_t>(msgtype.length());
    const size_t msg_size = REDP_HEADER_SIZE + 3 + topic_size + 1 + msgtype_size;
    std::string redp_msg(msg_size, '\0');

    // REDP 头部
    ::strcpy(redp_msg.data(), "ED01");
    ::memcpy(redp_msg.data() + 4, &endpoint_guid.full, sizeof(endpoint_guid.full));
    uint8_t action_type = static_cast<uint8_t>(static_cast<uint8_t>(action) | static_cast<uint8_t>(type));
    ::memcpy(redp_msg.data() + 4 + sizeof(endpoint_guid.full), &action_type, sizeof(action_type));

    // REDP 负载
    uint16_t net_port = htons(port);
    ::memcpy(redp_msg.data() + REDP_HEADER_SIZE, &net_port, sizeof(net_port));
    ::memcpy(redp_msg.data() + REDP_HEADER_SIZE + 2, &topic_size, sizeof(topic_size));
    ::memcpy(redp_msg.data() + REDP_HEADER_SIZE + 3, topic.data(), topic_size);
    ::memcpy(redp_msg.data() + REDP_HEADER_SIZE + 3 + topic_size, &msgtype_size, sizeof(msgtype_size));
    ::memcpy(redp_msg.data() + REDP_HEADER_SIZE + 3 + topic_size + 1, msgtype.data(), msgtype_size);
    return redp_msg;
}

REDPMessage REDPMessage::deserialize(const char *data) noexcept {
    REDPMessage res;
    // REDP 头部
    if (std::string_view(data, 4) != "ED01")
        return res;
    ::memcpy(&res.endpoint_guid.full, data + 4, sizeof(res.endpoint_guid.full));
    uint8_t action_type{};
    ::memcpy(&action_type, data + 4 + sizeof(res.endpoint_guid.full), sizeof(action_type));
    res.action = static_cast<Action>(action_type & 0b01);
    res.type = static_cast<Type>(action_type & 0b10);

    // REDP 负载
    uint16_t net_port{};
    ::memcpy(&net_port, data + REDP_HEADER_SIZE, sizeof(net_port));
    res.port = ntohs(net_port);
    uint8_t topic_size{};
    ::memcpy(&topic_size, data + REDP_HEADER_SIZE + 2, sizeof(topic_size));
    res.topic = std::string(data + REDP_HEADER_SIZE + 3, topic_size);
    uint8_t msgtype_size{};
    ::memcpy(&msgtype_size, data + REDP_HEADER_SIZE + 3 + topic_size, sizeof(msgtype_size));
    res.msgtype = std::string(data + REDP_HEADER_SIZE + 3 + topic_size + 1, msgtype_size);
    return res;
}

////////////////////////////////////////////////////////////////////////////////

void sendREDPMessage(Locator loc, const REDPMessage &msg) {
    static DgramSocket redp_sender = Sender(ip::udp::v4()).create();
    char addr_str[INET_ADDRSTRLEN]{};
    ::inet_ntop(AF_INET, &loc.addr, addr_str, INET_ADDRSTRLEN);
    redp_sender.write(addr_str, Endpoint(ip::udp::v4(), loc.port), msg.serialize());
}

Locator selectBestLocator(std::array<uint8_t, 4> target_ip, const RNDPMessage &rndp_msg) {
    for (const auto &loc : rndp_msg.locators)
        if (loc.addr == target_ip)
            return loc;
    return {};
}

} // namespace rm::lpss

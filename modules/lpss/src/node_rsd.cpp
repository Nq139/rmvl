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

#include <algorithm>
#include <cstring>
#include <random>
#include <unordered_set>

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
    // EDP 和 MTP 分别协商，当前只支持版本 2
    return {static_cast<uint8_t>(peer.edp_version >= 2 ? 2 : 0),
            static_cast<uint8_t>(peer.mtp_version >= 2 ? 2 : 0)};
}

bool sameREDPEndpoint(const REDPEndpoint &a, const REDPEndpoint &b) noexcept {
    return a.guid == b.guid && a.port == b.port &&
           a.topic == b.topic && a.msgtype == b.msgtype;
}

bool snapshotBelongsToPeer(Guid peer, const std::vector<REDPEndpoint> &snapshot) noexcept {
    for (const auto &endpoint : snapshot)
        if (!endpoint.guid.full || endpoint.guid.host() != peer.host() ||
            endpoint.guid.pid() != peer.pid())
            return false;
    return true;
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

////////////////////////////////////////////////////////////////////////////////
// REDP 报文与可靠同步

uint16_t initialREDPSyn() {
    std::mt19937 rng(std::random_device{}());
    return std::uniform_int_distribution<uint16_t>{0, UINT16_MAX}(rng);
}

bool countREDPEndpointSize(size_t &total, std::string_view topic,
                           std::string_view type) noexcept {
    constexpr size_t max_payload = 65507;
    if (topic.empty() || type.empty() || topic.size() > 63 || type.size() > 63)
        return false;
    const size_t entry_size = 12 + topic.size() + type.size();
    if (total > max_payload || entry_size > max_payload - total)
        return false;
    total += entry_size;
    return true;
}

std::string REDPMessage::serialize() const {
    if (snapshot.size() > UINT16_MAX || (ack && !snapshot.empty()))
        return {};
    size_t size = 17;
    std::unordered_set<uint64_t> ids{};
    for (const auto &ep : snapshot)
        if (!ep.guid.full || !ids.insert(ep.guid.full).second ||
            !countREDPEndpointSize(size, ep.topic, ep.msgtype))
            return {};

    std::string wire(size, '\0');
    ::memcpy(wire.data(), "ED02", 4);
    ::memcpy(wire.data() + 4, &source.full, 8);
    const auto net_syn = htons(syn);
    const auto net_count = htons(static_cast<uint16_t>(snapshot.size()));
    ::memcpy(wire.data() + 12, &net_syn, 2);
    wire[14] = ack ? 1 : 0;
    ::memcpy(wire.data() + 15, &net_count, 2);
    size_t off = 17;
    for (const auto &ep : snapshot) {
        ::memcpy(wire.data() + off, &ep.guid.full, 8);
        off += 8;
        const auto net_port = htons(ep.port);
        ::memcpy(wire.data() + off, &net_port, 2);
        off += 2;
        wire[off++] = static_cast<char>(ep.topic.size());
        ::memcpy(wire.data() + off, ep.topic.data(), ep.topic.size());
        off += ep.topic.size();
        wire[off++] = static_cast<char>(ep.msgtype.size());
        ::memcpy(wire.data() + off, ep.msgtype.data(), ep.msgtype.size());
        off += ep.msgtype.size();
    }
    return wire;
}

std::optional<REDPMessage> REDPMessage::deserialize(std::string_view wire) noexcept {
    if (wire.size() < 17 || wire.size() > 65507 || wire.substr(0, 4) != "ED02" ||
        static_cast<uint8_t>(wire[14]) > 1)
        return std::nullopt;
    REDPMessage result{};
    ::memcpy(&result.source.full, wire.data() + 4, 8);
    if (!result.source.full)
        return std::nullopt;
    uint16_t net_syn{}, net_count{};
    ::memcpy(&net_syn, wire.data() + 12, 2);
    ::memcpy(&net_count, wire.data() + 15, 2);
    result.syn = ntohs(net_syn);
    result.ack = wire[14] == 1;
    const auto count = ntohs(net_count);
    if ((result.ack && count != 0) || count > (wire.size() - 17) / 14)
        return std::nullopt;
    size_t off = 17;
    result.snapshot.reserve(count);
    std::unordered_set<uint64_t> ids{};
    for (size_t i = 0; i < count; ++i) {
        if (wire.size() - off < 12)
            return std::nullopt;
        REDPEndpoint ep{};
        ::memcpy(&ep.guid.full, wire.data() + off, 8);
        off += 8;
        uint16_t net_port{};
        ::memcpy(&net_port, wire.data() + off, 2);
        ep.port = ntohs(net_port);
        off += 2;
        const size_t topic_len = static_cast<uint8_t>(wire[off++]);
        if (topic_len == 0 || topic_len > 63 || topic_len >= wire.size() - off)
            return std::nullopt;
        ep.topic.assign(wire.data() + off, topic_len);
        off += topic_len;
        const size_t type_len = static_cast<uint8_t>(wire[off++]);
        if (type_len == 0 || type_len > 63 || type_len > wire.size() - off ||
            !ep.guid.full || !ids.insert(ep.guid.full).second)
            return std::nullopt;
        ep.msgtype.assign(wire.data() + off, type_len);
        off += type_len;
        result.snapshot.push_back(std::move(ep));
    }
    if (off != wire.size())
        return std::nullopt;
    return result;
}

std::optional<REDPMessage> REDPSender::request(
    const std::vector<REDPEndpoint> &snapshot) {
    if (_pending)
        return _pending;
    if (!_dirty)
        return std::nullopt;
    _pending = REDPMessage{_local, _next++, false, snapshot};
    _dirty = false;
    return _pending;
}

bool REDPSender::confirm(const REDPMessage &reply,
                                      const std::vector<REDPEndpoint> &snapshot) {
    if (!_pending || !reply.ack || !reply.snapshot.empty() ||
        !(reply.source == _peer) ||
        reply.syn != static_cast<uint16_t>(_pending->syn + 1))
        return false;
    // Node 快照遍历顺序固定，等待确认期间相互抵消的变化无需再次发送。
    _dirty = snapshot.size() != _pending->snapshot.size() ||
             !std::equal(snapshot.begin(), snapshot.end(), _pending->snapshot.begin(),
                         sameREDPEndpoint);
    _pending.reset();
    return true;
}

std::optional<REDPReceiver::Result> REDPReceiver::accept(
    const REDPMessage &request, Guid local) {
    if (request.ack || !(request.source == _peer))
        return std::nullopt;
    bool applied = !_initialized;
    if (_initialized) {
        if (request.syn == _last_syn) {
            if (request.snapshot.size() != _snapshot.size())
                return std::nullopt;
            for (size_t i = 0; i < _snapshot.size(); ++i) {
                const auto &a = request.snapshot[i], &b = _snapshot[i];
                if (a.guid.full != b.guid.full || a.port != b.port ||
                    a.topic != b.topic || a.msgtype != b.msgtype)
                    return std::nullopt;
            }
        } else if (request.syn == static_cast<uint16_t>(_last_syn + 1)) {
            applied = true;
        } else {
            return std::nullopt;
        }
    }
    if (applied) {
        _snapshot = request.snapshot;
        _last_syn = request.syn;
        _initialized = true;
    }
    return Result{applied, REDPMessage{local,
        static_cast<uint16_t>(request.syn + 1), true, {}}};
}

bool sendREDPMessage(DgramSocket &socket, Locator locator, const REDPMessage &message) {
    if (locator.invalid())
        return false;
    auto wire = message.serialize();
    if (wire.empty())
        return false;
    // 使用 RNDP 公告的控制端口发送
    return socket.write(locator.addr, Endpoint(ip::udp::v4(), locator.port), wire);
}

////////////////////////////////////////////////////////////////////////////////

Locator selectBestLocator(std::array<uint8_t, 4> target_ip, const RNDPMessage &rndp_msg) {
    for (const auto &loc : rndp_msg.locators)
        if (loc.addr == target_ip)
            return loc;
    return {};
}

} // namespace rm::lpss

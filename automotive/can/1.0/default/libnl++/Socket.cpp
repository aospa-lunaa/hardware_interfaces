/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <libnl++/Socket.h>

#include <libnl++/printer.h>

#include <android-base/logging.h>

namespace android::nl {

/**
 * Print all outbound/inbound Netlink messages.
 */
static constexpr bool kSuperVerbose = false;

Socket::Socket(int protocol, unsigned pid, uint32_t groups) : mProtocol(protocol) {
    mFd.reset(socket(AF_NETLINK, SOCK_RAW, protocol));
    if (!mFd.ok()) {
        PLOG(ERROR) << "Can't open Netlink socket";
        mFailed = true;
        return;
    }

    sockaddr_nl sa = {};
    sa.nl_family = AF_NETLINK;
    sa.nl_pid = pid;
    sa.nl_groups = groups;

    if (bind(mFd.get(), reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) < 0) {
        PLOG(ERROR) << "Can't bind Netlink socket";
        mFd.reset();
        mFailed = true;
    }
}

bool Socket::send(const Buffer<nlmsghdr>& msg, const sockaddr_nl& sa) {
    if constexpr (kSuperVerbose) {
        LOG(VERBOSE) << (mFailed ? "(not) " : "") << "sending Netlink message ("  //
                     << msg->nlmsg_pid << " -> " << sa.nl_pid << "): " << toString(msg, mProtocol);
    }
    if (mFailed) return false;

    mSeq = msg->nlmsg_seq;
    const auto rawMsg = msg.getRaw();
    const auto bytesSent = sendto(mFd.get(), rawMsg.ptr(), rawMsg.len(), 0,
                                  reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
    if (bytesSent < 0) {
        PLOG(ERROR) << "Can't send Netlink message";
        return false;
    } else if (size_t(bytesSent) != rawMsg.len()) {
        LOG(ERROR) << "Can't send Netlink message: truncated message";
        return false;
    }
    return true;
}

std::optional<Buffer<nlmsghdr>> Socket::receive(size_t maxSize) {
    return receiveFrom(maxSize).first;
}

std::pair<std::optional<Buffer<nlmsghdr>>, sockaddr_nl> Socket::receiveFrom(size_t maxSize) {
    if (mFailed) return {std::nullopt, {}};

    if (maxSize == 0) {
        LOG(ERROR) << "Maximum receive size should not be zero";
        return {std::nullopt, {}};
    }
    if (mReceiveBuffer.size() < maxSize) mReceiveBuffer.resize(maxSize);

    sockaddr_nl sa = {};
    socklen_t saLen = sizeof(sa);
    const auto bytesReceived = recvfrom(mFd.get(), mReceiveBuffer.data(), maxSize, MSG_TRUNC,
                                        reinterpret_cast<sockaddr*>(&sa), &saLen);

    if (bytesReceived <= 0) {
        PLOG(ERROR) << "Failed to receive Netlink message";
        return {std::nullopt, {}};
    } else if (size_t(bytesReceived) > maxSize) {
        PLOG(ERROR) << "Received data larger than maximum receive size: "  //
                    << bytesReceived << " > " << maxSize;
        return {std::nullopt, {}};
    }

    Buffer<nlmsghdr> msg(reinterpret_cast<nlmsghdr*>(mReceiveBuffer.data()), bytesReceived);
    if constexpr (kSuperVerbose) {
        LOG(VERBOSE) << "received (" << sa.nl_pid << " -> " << msg->nlmsg_pid << "):"  //
                     << toString(msg, mProtocol);
    }
    return {msg, sa};
}

bool Socket::receiveAck(uint32_t seq) {
    const auto nlerr = receive<nlmsgerr>({NLMSG_ERROR});
    if (!nlerr.has_value()) return false;

    if (nlerr->data.msg.nlmsg_seq != seq) {
        LOG(ERROR) << "Received ACK for a different message (" << nlerr->data.msg.nlmsg_seq
                   << ", expected " << seq << "). Multi-message tracking is not implemented.";
        return false;
    }

    if (nlerr->data.error == 0) return true;

    LOG(WARNING) << "Received Netlink error message: " << strerror(-nlerr->data.error);
    return false;
}

std::optional<Buffer<nlmsghdr>> Socket::receive(const std::set<nlmsgtype_t>& msgtypes,
                                                size_t maxSize) {
    while (!mFailed) {
        const auto msgBuf = receive(maxSize);
        if (!msgBuf.has_value()) return std::nullopt;

        for (const auto rawMsg : *msgBuf) {
            if (msgtypes.count(rawMsg->nlmsg_type) == 0) {
                LOG(WARNING) << "Received (and ignored) unexpected Netlink message of type "
                             << rawMsg->nlmsg_type;
                continue;
            }

            return rawMsg;
        }
    }

    return std::nullopt;
}

std::optional<unsigned> Socket::getPid() {
    if (mFailed) return std::nullopt;

    sockaddr_nl sa = {};
    socklen_t sasize = sizeof(sa);
    if (getsockname(mFd.get(), reinterpret_cast<sockaddr*>(&sa), &sasize) < 0) {
        PLOG(ERROR) << "Failed to get PID of Netlink socket";
        return std::nullopt;
    }
    return sa.nl_pid;
}

}  // namespace android::nl

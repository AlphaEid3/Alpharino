// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/chat/HotChat.hpp"

#include <QHash>

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>

namespace chatterino {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kRecent = std::chrono::seconds(8);
constexpr auto kBaseline = std::chrono::seconds(60);
constexpr auto kKeep = kRecent + kBaseline;

struct Series {
    std::deque<Clock::time_point> times;
    Clock::time_point started{};
};

std::mutex &hotMutex()
{
    static std::mutex mutex;
    return mutex;
}

QHash<QString, Series> &series()
{
    static QHash<QString, Series> values;
    return values;
}

QString keyFor(const QString &channelName)
{
    return channelName.trimmed().toLower();
}

void prune(Series &item, Clock::time_point now)
{
    const auto cutoff = now - kKeep;
    while (!item.times.empty() && item.times.front() < cutoff)
    {
        item.times.pop_front();
    }
}

int countBetween(const Series &item, Clock::time_point start,
                 Clock::time_point end)
{
    return static_cast<int>(std::count_if(
        item.times.begin(), item.times.end(), [start, end](const auto &time) {
            return time >= start && time < end;
        }));
}

}  // namespace

HotChat &HotChat::instance()
{
    static HotChat value;
    return value;
}

void HotChat::note(const QString &channelName)
{
    const auto key = keyFor(channelName);
    if (key.isEmpty())
    {
        return;
    }

    std::lock_guard lock(hotMutex());
    auto &item = series()[key];
    const auto now = Clock::now();
    if (item.times.empty())
    {
        item.started = now;
    }
    prune(item, now);
    item.times.push_back(now);
    constexpr int kCap = 2500;
    while (static_cast<int>(item.times.size()) > kCap)
    {
        item.times.pop_front();
    }
}

bool HotChat::isHot(const QString &channelName, int sensitivity) const
{
    const auto key = keyFor(channelName);
    if (key.isEmpty())
    {
        return false;
    }
    sensitivity = std::clamp(sensitivity, 1, 10);
    const double t = (sensitivity - 1) / 9.0;
    const double multiplier = 6.0 - (t * 4.4);
    const double minRate = 12.0 - (t * 10.0);

    std::lock_guard lock(hotMutex());
    const auto it = series().constFind(key);
    if (it == series().cend())
    {
        return false;
    }

    const auto now = Clock::now();
    const auto recentStart = now - kRecent;
    const auto baselineStart = now - kKeep;
    const auto recentCount = countBetween(*it, recentStart, now + kRecent);
    const auto baselineCount = countBetween(*it, baselineStart, recentStart);
    const double recentRate = recentCount / 8.0;
    const double baselineRate = baselineCount / 60.0;
    const auto observedFor = now - it->started;
    if (observedFor < std::chrono::seconds(20))
    {
        return recentRate >= minRate * 1.5;
    }
    return recentRate >= minRate && recentRate >= baselineRate * multiplier;
}

}  // namespace chatterino

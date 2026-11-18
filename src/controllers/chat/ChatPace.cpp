// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/chat/ChatPace.hpp"

#include <QHash>

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <vector>

namespace chatterino {
namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kWindow = std::chrono::seconds(2);
constexpr auto kKeep = std::chrono::seconds(4);

struct Series {
    std::deque<Clock::time_point> times;
};

std::mutex &paceMutex()
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

int countSince(const Series &item, Clock::time_point now,
               Clock::duration window)
{
    const auto cutoff = now - window;
    return static_cast<int>(std::count_if(item.times.begin(), item.times.end(),
                                          [cutoff](const auto &time) {
                                              return time >= cutoff;
                                          }));
}

}  // namespace

ChatPace &ChatPace::instance()
{
    static ChatPace value;
    return value;
}

void ChatPace::note(const QString &channelName)
{
    const auto key = keyFor(channelName);
    if (key.isEmpty())
    {
        return;
    }

    std::lock_guard lock(paceMutex());
    auto &item = series()[key];
    const auto now = Clock::now();
    item.times.push_back(now);
    prune(item, now);
    while (item.times.size() > 400)
    {
        item.times.pop_front();
    }
}

double ChatPace::messagesPerSecond(const QString &channelName) const
{
    const auto key = keyFor(channelName);
    std::lock_guard lock(paceMutex());
    const auto it = series().constFind(key);
    if (it == series().cend() || it->times.empty())
    {
        return 0;
    }
    const auto count = countSince(*it, Clock::now(), kWindow);
    return static_cast<double>(count) / 2.0;
}

bool ChatPace::burstEnding(const QString &channelName) const
{
    const auto key = keyFor(channelName);
    std::lock_guard lock(paceMutex());
    const auto it = series().constFind(key);
    if (it == series().cend())
    {
        return false;
    }

    const auto now = Clock::now();
    std::vector<Clock::time_point> recent;
    recent.reserve(it->times.size());
    const auto cutoff = now - kWindow;
    for (const auto &time : it->times)
    {
        if (time >= cutoff)
        {
            recent.push_back(time);
        }
    }
    if (recent.size() < 6)
    {
        return false;
    }

    std::vector<double> gaps;
    gaps.reserve(recent.size() - 1);
    for (size_t index = 1; index < recent.size(); ++index)
    {
        gaps.push_back(std::chrono::duration<double, std::milli>(
                           recent[index] - recent[index - 1])
                           .count());
    }
    std::sort(gaps.begin(), gaps.end());
    const auto median = gaps[gaps.size() / 2];
    const auto silence =
        std::chrono::duration<double, std::milli>(now - recent.back()).count();
    const auto burstGap = std::max(median * 2.5, 90.0);
    return silence >= burstGap;
}

}  // namespace chatterino

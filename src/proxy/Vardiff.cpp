/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2018-2025 SChernykh   <https://github.com/SChernykh>
 * Copyright 2016-2025 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include "proxy/Vardiff.h"


#include <algorithm>
#include <cinttypes>


#include "base/io/log/Log.h"
#include "base/io/log/Tags.h"
#include "base/tools/Chrono.h"
#include "core/config/Config.h"
#include "core/Controller.h"
#include "proxy/Miner.h"
#include "proxy/Miners.h"
#include "proxy/events/AcceptEvent.h"
#include "proxy/events/LoginEvent.h"
#include "proxy/interfaces/ISplitter.h"


xmrig::Vardiff::Vardiff(Controller *controller, const Miners *miners, ISplitter *splitter) :
    m_controller(controller),
    m_miners(miners),
    m_splitter(splitter)
{
}


xmrig::Vardiff::~Vardiff() = default;


void xmrig::Vardiff::onEvent(IEvent *event)
{
    switch (event->type())
    {
    case IEvent::LoginType:
        login(static_cast<LoginEvent*>(event));
        break;

    case IEvent::AcceptType:
        accept(static_cast<AcceptEvent*>(event));
        break;

    default:
        break;
    }
}


void xmrig::Vardiff::tick(uint64_t ticks)
{
    if ((ticks % kTickInterval) != 0 || !m_controller->config()->isVardiff()) {
        return;
    }

    const Config *config = m_controller->config();
    const uint64_t now     = Chrono::steadyMSecs();
    const uint64_t timeout = config->vardiffTarget() * kIdleMultiplier * 1000;

    for (Miner *miner : m_miners->miners()) {
        if (miner->routeId() != -1 || miner->customDiff() != 0 || miner->autoDiff() == 0) {
            continue;
        }

        const uint64_t last = miner->vardiffLastShare();
        if (last && (now - last) < timeout) {
            continue;
        }

        if (now - miner->vardiffLastChange() < kChangeCooldown) {
            continue;
        }

        uint64_t diff = std::max(config->vardiffMin(), miner->autoDiff() / 2);
        if (diff >= miner->autoDiff()) {
            continue;
        }

        resend(miner, diff, now);

        miner->setVardiffHashrate(miner->vardiffHashrate() / 2.0);
    }
}


void xmrig::Vardiff::login(LoginEvent *event)
{
    Miner *miner = event->miner();

    if (!m_controller->config()->isVardiff() || miner->routeId() != -1 || miner->customDiff() != 0) {
        return;
    }

    const uint64_t now = Chrono::steadyMSecs();
    miner->setVardiffLastShare(now);
    miner->setVardiffLastChange(now);
    miner->setVardiffHashrate(0.0);

    uint64_t diff = kStartDiff;
    const uint64_t min = m_controller->config()->vardiffMin();
    if (diff < min) {
        diff = min;
    }

    miner->setAutoDiff(diff);
}


void xmrig::Vardiff::accept(AcceptEvent *event)
{
    if (!m_controller->config()->isVardiff()) {
        return;
    }

    Miner *miner = event->miner();
    if (!miner || miner->routeId() != -1 || miner->customDiff() != 0) {
        return;
    }

    const uint64_t now  = Chrono::steadyMSecs();
    const uint64_t prev = miner->vardiffLastShare();
    miner->setVardiffLastShare(now);

    if (event->isRejected() || event->isDonate() || !event->isCustomDiff() || prev == 0) {
        return;
    }

    uint64_t dt = now > prev ? std::min(now - prev, kMaxShareGap) : 1000;
    if (dt < 1000) {
        dt = 1000;
    }

    const double hashrate = static_cast<double>(event->result.actualDiff) / (dt / 1000.0);
    if (hashrate <= 0.0) {
        return;
    }

    const double est = miner->vardiffHashrate();
    miner->setVardiffHashrate(est > 0.0 ? est * 0.7 + hashrate * 0.3 : hashrate);

    update(miner, now);
}


bool xmrig::Vardiff::update(Miner *miner, uint64_t now)
{
    const Config *config = m_controller->config();

    uint64_t diff = static_cast<uint64_t>(miner->vardiffHashrate() * config->vardiffTarget());

    uint64_t maxDiff = config->vardiffMax();
    if (maxDiff == 0 || maxDiff > miner->poolDiff()) {
        maxDiff = miner->poolDiff();
    }

    uint64_t min = config->vardiffMin();
    if (min > maxDiff) {
        min = maxDiff;
    }

    diff = std::min(std::max(diff, min), maxDiff);

    const uint64_t cur = miner->autoDiff();

    if (diff == cur) {
        return false;
    }

    if (now - miner->vardiffLastChange() < kChangeCooldown) {
        return false;
    }

    if (cur != 0) {
        if (diff > cur) {
            if (diff < cur + cur / 8) {
                return false;
            }
        }
        else if (diff > cur - cur / 8) {
            return false;
        }
    }

    resend(miner, diff, now);

    return true;
}


void xmrig::Vardiff::resend(Miner *miner, uint64_t diff, uint64_t now)
{
    const uint64_t prev = miner->autoDiff();

    miner->setAutoDiff(diff);
    miner->setVardiffLastChange(now);
    m_splitter->resendJob(miner);

    if (prev != 0) {
        LOG_INFO("%s \x1B[01;36m#%" PRId64 "\x1B[0m " WHITE_BOLD("%s") " vardiff " WHITE_BOLD("%" PRIu64) " \x1B[1;30m->\x1B[0m %s%" PRIu64 "\x1B[0m",
                 Tags::proxy(), miner->id(), miner->ip(), prev, diff > prev ? "\x1B[01;32m" : "\x1B[01;33m", diff);
    }
}

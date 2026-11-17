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

#ifndef XMRIG_VARDIFF_H
#define XMRIG_VARDIFF_H


#include <cstdint>


#include "base/tools/Object.h"
#include "proxy/interfaces/IEventListener.h"


namespace xmrig {


class AcceptEvent;
class Controller;
class ISplitter;
class LoginEvent;
class Miner;
class Miners;


class Vardiff : public IEventListener
{
public:
    XMRIG_DISABLE_COPY_MOVE_DEFAULT(Vardiff)

    Vardiff(Controller *controller, const Miners *miners, ISplitter *splitter);
    ~Vardiff() override;

    void tick(uint64_t ticks);

protected:
    inline void onRejectedEvent(IEvent *) override {}

    void onEvent(IEvent *event) override;

private:
    constexpr static uint64_t kStartDiff       = 100000;
    constexpr static uint64_t kChangeCooldown  = 15 * 1000;
    constexpr static uint64_t kMaxShareGap     = 5 * 60 * 1000;
    constexpr static uint64_t kIdleMultiplier  = 2;
    constexpr static uint64_t kTickInterval    = 5;

    void login(LoginEvent *event);
    void accept(AcceptEvent *event);

    bool update(Miner *miner, uint64_t now);
    void resend(Miner *miner, uint64_t diff, uint64_t now);

    Controller *m_controller;
    const Miners *m_miners;
    ISplitter *m_splitter;
};


} // namespace xmrig


#endif // XMRIG_VARDIFF_H

/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2017-2018 XMR-Stak    <https://github.com/fireice-uk>, <https://github.com/psychocrypt>
 * Copyright 2018      Lee Clagett <https://github.com/vtnerd>
 * Copyright 2018-2019 SChernykh   <https://github.com/SChernykh>
 * Copyright 2016-2019 XMRig       <https://github.com/xmrig>, <support@xmrig.com>
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


#include <stdlib.h>
#include <uv.h>
#include <cc/ControlCommand.h>


#include "App.h"
#include "backend/cpu/Cpu.h"
#include "base/io/Console.h"
#include "base/io/log/Log.h"
#include "base/kernel/Signals.h"
#include "core/config/Config.h"
#include "core/Controller.h"
#include "core/Miner.h"
#include "crypto/common/VirtualMemory.h"
#include "net/Network.h"
#include "Summary.h"

xmrig::App::App(Process *process)
{
    m_controller = new Controller(process);
}


xmrig::App::~App()
{
    Cpu::release();

    delete m_signals;
    delete m_console;
    delete m_controller;
}


int xmrig::App::exec()
{
    if (!m_controller->isReady()) {
        LOG_EMERG("no valid configuration found.");

        return 2;
    }

#   ifdef XMRIG_FEATURE_CC_CLIENT
    if (!m_controller->config()->isDaemonized()) {
        LOG_EMERG(APP_ID " is compiled with CC support, please start the daemon instead.\n");

        return 2;
    }
#   endif

    m_signals = new Signals(this);

    int rc = 0;
    if (background(rc)) {
        return rc;
    }

    rc = m_controller->init();
    if (rc != 0) {
        return rc;
    }

    if (!m_controller->isBackground()) {
        m_console = new Console(this);
    }

    Summary::print(m_controller);

    if (m_controller->config()->isDryRun()) {
        LOG_NOTICE("OK");

        return 0;
    }

    m_controller->start();

#   if XMRIG_FEATURE_CC_CLIENT
    m_controller->ccClient()->addCommandListener(this);
#   endif

    rc = uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    uv_loop_close(uv_default_loop());

    return m_restart ? EINTR : rc;
}


void xmrig::App::onConsoleCommand(char command)
{
    switch (command) {
    case 'h':
    case 'H':
        m_controller->miner()->printHashrate(true);
        break;

    case 'p':
    case 'P':
        m_controller->miner()->setEnabled(false);
        break;

    case 'r':
    case 'R':
        m_controller->miner()->setEnabled(true);
        break;

    case 'q':
        close(false);
        break;

    case 3:
        LOG_WARN("Ctrl+C received, exiting");
        close(false);
        break;

    default:
        break;
    }
}


void xmrig::App::onSignal(int signum)
{
    switch (signum)
    {
    case SIGHUP:
        LOG_WARN("SIGHUP received, exiting");
        break;

    case SIGTERM:
        LOG_WARN("SIGTERM received, exiting");
        break;

    case SIGINT:
        LOG_WARN("SIGINT received, exiting");
        break;

    default:
        return;
    }

    close(false);
}

void xmrig::App::onCommandReceived(ControlCommand::Command command)
{
#   ifdef XMRIG_FEATURE_CC_CLIENT
    switch (command) {
        case ControlCommand::START:
            m_controller->miner()->setEnabled(true);
            break;
        case ControlCommand::STOP:
            m_controller->miner()->setEnabled(false);
            break;
        case ControlCommand::RESTART:
            close(true);
            break;
        case ControlCommand::SHUTDOWN:
            close(false);
            break;
        case ControlCommand::REBOOT:
            reboot();
        case ControlCommand::UPDATE_CONFIG:;
        case ControlCommand::PUBLISH_CONFIG:;
            break;
    }
#   endif
}

void xmrig::App::close(bool restart)
{
    m_restart = restart;

    m_controller->stop();

    m_signals->stop();

    if (m_console) {
        m_console->stop();
    }

    Log::destroy();
    
    uv_stop(uv_default_loop());
}

#   ifdef XMRIG_FEATURE_CC_CLIENT
void xmrig::App::reboot()
{
    auto rebootCmd = m_controller->config()->ccClient().rebootCmd();
    if (rebootCmd) {
        system(rebootCmd);
        close(false);
    }
}
#   endif

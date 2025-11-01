/**
 * OpenAL cross platform audio library
 * Copyright (C) 2011 by Chris Robinson
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "loopback.h"

#include "core/device.h"


namespace {

struct LoopbackBackend final : BackendBase {
    explicit LoopbackBackend(gsl::not_null<DeviceBase*> const device) noexcept
        : BackendBase{device}
    { }

    void open(std::string_view name) override;
    auto reset() -> bool override;
    void start() override;
    void stop() override;
};


void LoopbackBackend::open(std::string_view const name)
{
    mDeviceName = name;
}

auto LoopbackBackend::reset() -> bool
{
    setDefaultWFXChannelOrder();
    return true;
}

void LoopbackBackend::start()
{ }

void LoopbackBackend::stop()
{ }

} // namespace


auto LoopbackBackendFactory::init() -> bool
{ return true; }

auto LoopbackBackendFactory::querySupport(BackendType) -> bool
{ return true; }

auto LoopbackBackendFactory::enumerate(BackendType) -> std::vector<std::string>
{ return {}; }

auto LoopbackBackendFactory::createBackend(gsl::not_null<DeviceBase*> const device, BackendType)
    -> BackendPtr
{ return BackendPtr{new LoopbackBackend{device}}; }

auto LoopbackBackendFactory::getFactory() -> BackendFactory&
{
    static LoopbackBackendFactory factory{};
    return factory;
}

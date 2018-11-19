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

#include "backends/loopback.h"

#include "alMain.h"
#include "alu.h"


namespace {

struct ALCloopback final : public ALCbackend {
};

void ALCloopback_Construct(ALCloopback *self, ALCdevice *device);
void ALCloopback_Destruct(ALCloopback *self);
ALCenum ALCloopback_open(ALCloopback *self, const ALCchar *name);
ALCboolean ALCloopback_reset(ALCloopback *self);
ALCboolean ALCloopback_start(ALCloopback *self);
void ALCloopback_stop(ALCloopback *self);
DECLARE_FORWARD2(ALCloopback, ALCbackend, ALCenum, captureSamples, void*, ALCuint)
DECLARE_FORWARD(ALCloopback, ALCbackend, ALCuint, availableSamples)
DECLARE_FORWARD(ALCloopback, ALCbackend, ClockLatency, getClockLatency)
DECLARE_FORWARD(ALCloopback, ALCbackend, void, lock)
DECLARE_FORWARD(ALCloopback, ALCbackend, void, unlock)
DECLARE_DEFAULT_ALLOCATORS(ALCloopback)
DEFINE_ALCBACKEND_VTABLE(ALCloopback);


void ALCloopback_Construct(ALCloopback *self, ALCdevice *device)
{
    new (self) ALCloopback{};
    ALCbackend_Construct(STATIC_CAST(ALCbackend, self), device);
    SET_VTABLE2(ALCloopback, ALCbackend, self);
}

void ALCloopback_Destruct(ALCloopback *self)
{
    ALCbackend_Destruct(STATIC_CAST(ALCbackend, self));
    self->~ALCloopback();
}


ALCenum ALCloopback_open(ALCloopback *self, const ALCchar *name)
{
    ALCdevice *device = STATIC_CAST(ALCbackend, self)->mDevice;

    device->DeviceName = name;
    return ALC_NO_ERROR;
}

ALCboolean ALCloopback_reset(ALCloopback *self)
{
    SetDefaultWFXChannelOrder(STATIC_CAST(ALCbackend, self)->mDevice);
    return ALC_TRUE;
}

ALCboolean ALCloopback_start(ALCloopback* UNUSED(self))
{
    return ALC_TRUE;
}

void ALCloopback_stop(ALCloopback* UNUSED(self))
{
}

} // namespace


bool LoopbackBackendFactory::init()
{ return true; }

bool LoopbackBackendFactory::querySupport(ALCbackend_Type type)
{ return (type == ALCbackend_Loopback); }

void LoopbackBackendFactory::probe(enum DevProbe, std::string*)
{ }

ALCbackend *LoopbackBackendFactory::createBackend(ALCdevice *device, ALCbackend_Type type)
{
    if(type == ALCbackend_Loopback)
    {
        ALCloopback *backend;
        NEW_OBJ(backend, ALCloopback)(device);
        if(!backend) return nullptr;
        return STATIC_CAST(ALCbackend, backend);
    }

    return nullptr;
}

BackendFactory &LoopbackBackendFactory::getFactory()
{
    static LoopbackBackendFactory factory{};
    return factory;
}

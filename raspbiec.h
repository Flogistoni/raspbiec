/*
 * Raspbiec - Commodore 64 & 1541 serial bus handler for Raspberry Pi
 * Copyright (C) 2013 Antti Paarlahti <antti.paarlahti@outlook.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef RASPBIEC_H
#define RASPBIEC_H

#include <stdint.h>
#include <vector>
#include <string>
#include "raspbiec_common.h"
#include "raspbiec_device.h"
#include "raspbiec_diskimage.h"

class computer
{
public:
    computer(pipefd &bus, const bool foreground);
    ~computer();
    void load(const char *filename, int device_number);
    void save(const char *filename, int device_number);
    void command(const char *command, int device_number);
    void read_error_channel(int device_number);
private:
    device m_dev;
    bool m_foreground;
};


#endif // RASPBIEC_H

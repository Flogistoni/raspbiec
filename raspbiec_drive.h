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

#ifndef RASPBIEC_DRIVE_H
#define RASPBIEC_DRIVE_H

#include <stdio.h>
#include <vector>
#include <string>
#include <sys/stat.h>
#include "raspbiec_common.h"
#include "raspbiec_device.h"
#include "raspbiec_diskimage.h"

class drive
{
public:
	drive(const int device_number, pipefd &bus, bool foreground);
	~drive();
	void serve(const char *path);

	enum usercommand
	{
		UC_NONE,
		UC_NEW,
		UC_SCRATCH,
		UC_RENAME,
		UC_COPY,
		UC_UTIL_LDR,
		UC_POSITION,
		UC_USER,
		UC_BLOCK_ALLOCATE,
		UC_BLOCK_FREE,
		UC_BLOCK_READ,
		UC_BLOCK_WRITE,
		UC_BLOCK_EXECUTE,
		UC_BUFFER_POINTER,
		UC_MEMORY_READ,
		UC_MEMORY_WRITE,
		UC_MEMORY_EXECUTE,
		UC_DUPLICATE,
		UC_INITIALIZE,
		UC_VALIDATE,
	};

	struct channel
	{
		int number;
		device::Command buscmd;
		usercommand usrcmd;
		bool open;
		std::vector<unsigned char> petscii; // Name or command
		std::string ascii; // Copy of the above
		// Temporary data buffer, only contains data which has
		// not yet been sent to bus or written to disk
		std::vector<unsigned char> data;
		// Info deduced from the name or command
		std::vector<unsigned char> name;
		std::vector<unsigned char> command;
		unsigned char rwam;
		unsigned char type;
		int fd; // File descriptor for open

		// Local file
		int mode;
		struct stat sb;
	};

private:
	void reset_channel(channel &ch);
	void reset_channels();
	void open_file(channel &ch);
	void close_file(channel &ch);
	void read_from_disk(channel &ch);
	void write_to_disk(channel &ch);
	void receive_name_or_command(channel &ch);
	int determine_command(channel &ch);
	int execute_command(channel &ch);


private:
	device m_dev;
	int m_device_number;
	channel channels[16];
	bool m_imagemode;
	Diskimage m_img;
	bool m_foreground;
};

#endif // RASPBIEC_DRIVE_H

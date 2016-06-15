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

#ifndef RASPBIEC_UTILS_H
#define RASPBIEC_UTILS_H

#include <vector>
#include <string>
#include "raspbiec_diskimage.h"
#include "raspbiec_common.h"
#include "raspbiec_types.h"

bool ispetsciinum(const unsigned char c);
bool ispetsciialpha(const unsigned char c);
bool ispetsciialnum(const unsigned char c);

const char petscii2ascii(const unsigned char petschar);
void petscii2ascii(const std::vector<unsigned char>& petschar,
		   std::string& ascchar);

const unsigned char ascii2petscii(const char ascchar);
void ascii2petscii( const std::string& ascchar,
		    std::vector<unsigned char>& petschar );

std::vector<unsigned char>::iterator petsciialnum(
	std::vector<unsigned char>::iterator petiter,
	const std::vector<unsigned char>::iterator petend,
	std::vector<unsigned char>& petstr);
	
void basic_listing(const databuf_t &prg);

size_t read_local_file(databuf_t &data, const char *name);

void write_local_file(const databuf_t &data, const char *name);

int open_local_file(const char *name, const char* mode);

void close_local_file(int& handle);

bool local_file_exists(const char *name);

// Read <amount> of data from file, replace data in <data>
size_t read_from_local_file(const int handle, databuf_t &data, size_t amount);
// Write <data> to file, return written amount
const_databuf_iter write_to_local_file(const int handle, const_databuf_iter begin, const_databuf_iter end);

void read_local_dir(databuf_t &buf, const char *dirname, bool verbose);

void read_diskimage_dir(databuf_t &buf, Diskimage& diskimage, bool verbose);

class pipefd
{
public:
	pipefd();
	~pipefd();
	void move(pipefd &other);
	void open_pipe();
	void open_dev();
	void close_pipe();
	bool is_open_directional();
	bool is_open_nondirectional();
	bool is_device();
	int write_end();
	int read_end();
	void set_direction_A_to_B() { set_direction(true); }
	void set_direction_B_to_A() { set_direction(false); }
private:
	bool all_open();
	void set_write(int *fd);
	void set_read(int *fd);
	void set_direction(bool fwd);

	int m_fd[4];
	int m_fd_size; // 1 == dev, 4 == two pipes
};

#endif // RASPBIEC_UTILS_H

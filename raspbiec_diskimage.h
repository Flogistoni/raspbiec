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

#ifndef RASPBIEC_DISKIMAGE_H
#define RASPBIEC_DISKIMAGE_H

#include <vector>
#include <string>

struct Diskentry;
struct BAMentry;
struct Dataentry;

class Diskimage
{
public:
	Diskimage();
	~Diskimage();

	void open(const char *path);
	void close();
	void flush();

	unsigned char *block(int track, int sector);

	size_t read_file( std::vector<unsigned char>& data,
			std::vector<unsigned char>& petsciiname );
	size_t write_file( std::vector<unsigned char>& data,
			std::vector<unsigned char>& petsciiname );
	int blocks_free();
	bool block_is_allocated(int track, int sector);
	void set_block_allocation(int track, int sector, bool alloc);
	bool track_is_full(int track);
	bool find_first_free_block(int& track, int& sector);
	bool find_next_free_block(int& track, int& sector, int interleave);
	int open_file(std::vector<unsigned char>& petsciiname);
	bool close_file(int handle);


	class Direntry_state
	{
	public:
		Direntry_state();
		Direntry_state(int track, int sector, int entry);
		int track;
		int sector;
		int entry;
	};

	class Dirstate
	{
	public:
		unsigned char name_id[27];
		int free_lo;
		int free_hi;

		Direntry_state de_state;
	};

	struct Direntry
	{
		unsigned char link_track;	// 0x00
		unsigned char link_sector; 	// 0x01
		unsigned char filetype;		// 0x02
		unsigned char first_track;	// 0x03
		unsigned char first_sector;	// 0x04
		unsigned char name[16];		// 0x05
		unsigned char relss_track;	// 0x15
		unsigned char relss_sector;	// 0x16
		unsigned char rel_reclen;	// 0x17
		unsigned char reserved[6];	// 0x18
		unsigned char size_lo;		// 0x1e
		unsigned char size_hi;		// 0x1f
	};

	bool opendir( Dirstate& dirstate );
	bool readdir( Dirstate& dirstate, Direntry& direntry );
	// No closedir() needed

private:
	Direntry* find_matching_direntry(
			bool (*matcher)(Direntry *d, int entry_index, void *userdata),
			void *userdata,
			Direntry_state *de_state );

	bool valid_ts(int track, int sector);
	int block_number(int track, int sector);
	size_t block_offset(int track, int sector);

private:
	std::vector<unsigned char> m_image;
	std::string m_imagename;

	int m_disktype;
	Diskentry *m_disk_block;

	bool m_dirty;
	bool m_mounted;
};


#endif // RASPBIEC_DISKIMAGE_H

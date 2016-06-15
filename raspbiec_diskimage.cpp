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

#include <cstdio>
#include <cstddef>
#include "raspbiec_diskimage.h"
#include "raspbiec_exception.h"
#include "raspbiec_common.h"
#include "raspbiec_utils.h"

#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

class Diskinfo
	{
	public:
		size_t image_size;
		int first_track;
		int last_track;
		int dir_track;
		int dir_sector;
		int bam_track;
		int bam_sector;
		/* For block allocation algorithm */
		int interleave;
		int dir_interleave;
		bool data_to_dir_track;
		bool geos_disk;
		/* 1571 has two dir tracks*/
		int dir_track2;
	};

static const Diskinfo diskinfo[] =
{
		//   Disk type                  Size
		//   ---------                  ------
		//   35 track, no errors        174848
		{
				174848,
				1, 35,
				18, 1,
				18, 0,
				10, 3,
				false,
				false,
				-1,
		},
		//   35 track, 683 error bytes  175531
		{
				175531,
				1, 35,
				18, 1,
				18, 0,
				10, 3,
				false,
				false,
				-1,
		},
		//   40 track, no errors        196608
		//   40 track, 768 error bytes  197376
};

struct Trackinfo
{
	int sectors_per_track;
	size_t track_offset; /* in blocks */
};

static const Trackinfo trackinfo[] =
{
		/*  - */   0,     0x000,
		/*  1 */  21,     0x000,
		/*  2 */  21,     0x015,
		/*  3 */  21,     0x02A,
		/*  4 */  21,     0x03F,
		/*  5 */  21,     0x054,
		/*  6 */  21,     0x069,
		/*  7 */  21,     0x07E,
		/*  8 */  21,     0x093,
		/*  9 */  21,     0x0A8,
		/* 10 */  21,     0x0BD,
		/* 11 */  21,     0x0D2,
		/* 12 */  21,     0x0E7,
		/* 13 */  21,     0x0FC,
		/* 14 */  21,     0x111,
		/* 15 */  21,     0x126,
		/* 16 */  21,     0x13B,
		/* 17 */  21,     0x150,
		/* 18 */  19,     0x165,
		/* 19 */  19,     0x178,
		/* 20 */  19,     0x18B,
		/* 21 */  19,     0x19E,
		/* 22 */  19,     0x1B1,
		/* 23 */  19,     0x1C4,
		/* 24 */  19,     0x1D7,
		/* 25 */  18,     0x1EA,
		/* 26 */  18,     0x1FC,
		/* 27 */  18,     0x20E,
		/* 28 */  18,     0x220,
		/* 29 */  18,     0x232,
		/* 30 */  18,     0x244,
		/* 31 */  17,     0x256,
		/* 32 */  17,     0x267,
		/* 33 */  17,     0x278,
		/* 34 */  17,     0x289,
		/* 35 */  17,     0x29A,
		/* 36 */  17,     0x2AB,
		/* 37 */  17,     0x2BC,
		/* 38 */  17,     0x2CD,
		/* 39 */  17,     0x2DE,
		/* 40 */  17,     0x2EF,
};

struct Diskentry
{
	unsigned char dir_track;
	unsigned char dir_sector;
	unsigned char dos_version;
	unsigned char unused;
	unsigned char BAM[0x8C];
	unsigned char name_id[0x1B];
	unsigned char extended[0x55];
};

struct BAMentry
{
	unsigned char free;
	unsigned char bitmap[3];
};

struct Dataentry
{
	unsigned char link_track;
	unsigned char link_sector;
	unsigned char data[254];
	static const size_t size = sizeof data;
};


/*
           00-01: Track/Sector location of next directory sector ($00 $00 if
               not the first entry in the sector)
           02: File type.
               Typical values for this location are:
                 $00 - Scratched (deleted file entry)
                  80 - DEL
                  81 - SEQ
                  82 - PRG
                  83 - USR
                  84 - REL
               Bit 0-3: The actual filetype
                        000 (0) - DEL
                        001 (1) - SEQ
                        010 (2) - PRG
                        011 (3) - USR
                        100 (4) - REL
                        Values 5-15 are illegal, but if used will produce
                        very strange results. The 1541 is inconsistent in
                        how it treats these bits. Some routines use all 4
                        bits, others ignore bit 3,  resulting  in  values
                        from 0-7.
               Bit   4: Not used
               Bit   5: Used only during SAVE-@ replacement
               Bit   6: Locked flag (Set produces ">" locked files)
               Bit   7: Closed flag  (Not  set  produces  "*", or "splat"
                        files)
        03-04: Track/sector location of first sector of file
        05-14: 16 character filename (in PETASCII, padded with $A0)
        15-16: Track/Sector location of first side-sector block (REL file
               only)
           17: REL file record length (REL file only, max. value 254)
        18-1D: Unused (except with GEOS disks)
        1E-1F: File size in sectors, low/high byte  order  ($1E+$1F*256).
               The approx. filesize in bytes is <= #sectors * 254
*/
enum Filetype
{
	FILE_DEL       = 0x0,
	FILE_SEQ       = 0x1,
	FILE_PRG       = 0x2,
	FILE_USR       = 0x3,
	FILE_REL       = 0x4,
	FILE_LOCKED = 1<<6,
	FILE_CLOSED = 1<<7,
};



bool Diskimage::valid_ts(int track, int sector)
{
	if (!m_mounted)
		return false;

	if (track < diskinfo[m_disktype].first_track || track > diskinfo[m_disktype].last_track)
		return false;

	if (sector < 0 || sector >= trackinfo[track].sectors_per_track)
		return false;

	return true;
}

int Diskimage::block_number(int track, int sector)
{
	if (!valid_ts(track, sector))
		throw raspbiec_error(IEC_ILLEGAL_TRACK_SECTOR);

	return trackinfo[track].track_offset + sector;
}

size_t Diskimage::block_offset(int track, int sector)
{
	return 0x100 * block_number(track, sector);
}

Diskimage::Diskimage() :
		m_disktype(-1),
		m_disk_block(NULL),
		m_dirty(false),
		m_mounted(false)
{
}

Diskimage::~Diskimage()
{
	close();
}

void Diskimage::open(const char *path)
{
	close();
	m_disktype = -1;
	m_imagename = path;
	read_local_file(m_image, m_imagename.c_str());
	for (unsigned int i=0; i<COUNT_OF(diskinfo); ++i)
	{
		if (m_image.size() == diskinfo[i].image_size)
		{
			m_disktype = i;
			m_mounted = true;
			// Cache a pointer to BAM etc.
			m_disk_block =
					(Diskentry *)block(diskinfo[m_disktype].bam_track, diskinfo[m_disktype].bam_sector);
			break;
		}
	}

	if (!m_mounted)
		throw raspbiec_error(IEC_UNKNOWN_DISK_IMAGE);
}

void Diskimage::close()
{
	if (m_mounted)
	{
		flush();
		m_image.clear();
		m_imagename.clear();
		m_mounted = false;
	}
}

void Diskimage::flush()
{
	if (m_dirty)
	{
		write_local_file(m_image, m_imagename.c_str());
	}
}

unsigned char *Diskimage::block(int track, int sector)
{
	size_t offset = block_offset(track, sector);
	if (offset >= m_image.size())
		throw raspbiec_error(IEC_ILLEGAL_TRACK_SECTOR);

	return m_image.data() + offset;
}

static bool match_name(
		std::vector<unsigned char>& petsciiname,
		unsigned char* dirname)
{
	for (unsigned int j=0; j < 16; ++j)
	{
		unsigned char c = dirname[j];

		if (c == 0xA0) // shift-space (name end marker)
			return (j == petsciiname.size());

		if (j >= petsciiname.size())
			return false;

		unsigned char cs = petsciiname[j];

		if (cs == 0x2A) // '*'
			return true;

		if (cs == 0x3F) // '?'
			continue;

        // http://sta.c64.org/cbm64pet.html
        // "Codes $60-$7F and $E0-$FE are not used. Although you can print them, these are, actually, copies of codes $C0-$DF and $A0-$BE"
        if (c  >= 0x60 && c  <= 0x7F) c  += 0x60;
        if (c  >= 0xE0 && c  <= 0xFE) c  -= 0x40;
        if (cs >= 0x60 && cs <= 0x7F) cs += 0x60;
        if (cs >= 0xE0 && cs <= 0xFE) cs -= 0x40;

		if (c != cs)
			return false;
	}
	return true;
}

int Diskimage::blocks_free()
{
	BAMentry *be = (BAMentry *)m_disk_block->BAM;

	int free = 0;
	for (int track = diskinfo[m_disktype].first_track; track <= diskinfo[m_disktype].last_track; ++track)
	{
		if (diskinfo[m_disktype].data_to_dir_track ||
				(track != diskinfo[m_disktype].dir_track &&
						track != diskinfo[m_disktype].dir_track2))
		{
			free += be[track - diskinfo[m_disktype].first_track].free;
		}
	}
	return free;
}

bool Diskimage::block_is_allocated(int track, int sector)
{
	if (!valid_ts(track, sector))
		return true;

	BAMentry *be = (BAMentry *)m_disk_block->BAM;

	return !(be[track-1].bitmap[sector/8] & (1 << (sector & 7)));
}

void Diskimage::set_block_allocation(int track, int sector, bool alloc)
{
	if (!valid_ts(track, sector))
	{
		// TODO print diagnostic message
		return;
	}
	BAMentry *be = (BAMentry *)m_disk_block->BAM;

	// Pointer to the byte containing the allocation bit for the block
	unsigned char* bp = &be[track-1].bitmap[sector/8];
	unsigned char bm = 1 << (sector & 7); // Bitmask for the bit

	bool BAM_alloc = !(*bp & bm);

	if (alloc && !BAM_alloc)
	{
		*bp &= ~bm; // Allocate a free block - clear bit
		--be[track-1].free;
	}
	else if (!alloc && BAM_alloc)
	{
		*bp |= bm; // Free an allocated block - set bit
		++be[track-1].free;
	}
}

bool Diskimage::track_is_full(int track)
{
	if (track < diskinfo[m_disktype].first_track ||	track > diskinfo[m_disktype].last_track)
		return true;

	BAMentry *be = (BAMentry *)m_disk_block->BAM;

	return be[track-1].free == 0;
}

/* in: none (track & sector ignored)
 * return true + free track & sector if found
 * return false if no free blocks found
 * See http://unusedino.de/ec64/technical/formats/disk.html
 * for algorithm description
 */
bool Diskimage::find_first_free_block(int& track, int& sector)
{
	if (diskinfo[m_disktype].geos_disk)
	{
		track = 1;
		sector = 0;
		// 8 sectors for 1541 disks
		return find_next_free_block(track, sector, 8);
	}

	bool found = false;
	int distance = 0;
	while (!found)
	{
		// Distance sequence: -1, 1, -2, 2, -3, 3, ...
		distance = (distance < 0) ? -distance : -(distance+1);

		track = diskinfo[m_disktype].dir_track + distance;
		if (track < diskinfo[m_disktype].first_track &&
				track > diskinfo[m_disktype].last_track)
		{
			break; // Both directions are off the disk
		}
		found = !track_is_full(track);
	}

	if (!found && diskinfo[m_disktype].data_to_dir_track)
	{
		track = diskinfo[m_disktype].dir_track;
		found = !track_is_full(track);
	}

	if (found)
	{
		for (sector = 0; sector < trackinfo[track].sectors_per_track; ++sector)
		{
			found = !block_is_allocated(track, sector);
			if (found) break;
		}
	}

	return found;
}

/* in: current track & sector, required interleave
 * return true + next free track & sector if found
 * return false if no free blocks
 * See http://unusedino.de/ec64/technical/formats/disk.html
 * for algorithm description
 */
bool Diskimage::find_next_free_block(int& track, int& sector, int interleave)
{
	if (track < diskinfo[m_disktype].first_track || track > diskinfo[m_disktype].last_track)
		return false;

	int tries = 3;
	bool found = false;
	int cur_track = track;

	while (!found && tries > 0)
	{
		if (!track_is_full(track))
		{
			if (track == cur_track || !diskinfo[m_disktype].geos_disk)
			{
				sector += interleave;
				if (diskinfo[m_disktype].geos_disk && track >= 25) --sector;
			}
			else // GEOS skew
			{
				sector = ((track - cur_track) << 1) + 4 + interleave;
			}
			int sectors_per_track = trackinfo[track].sectors_per_track;
			while (sector >= sectors_per_track)
			{
				sector -= sectors_per_track;
				if (sector > 0 && !diskinfo[m_disktype].geos_disk) --sector;
			}
			int cur_sector = sector;
			do
			{
				found = !block_is_allocated(track, sector);
				if (!found) ++sector;
				if (sector >= sectors_per_track) sector = 0;
			} while (!found && sector != cur_sector);
		}
		else // track full, try another
		{
			if (diskinfo[m_disktype].geos_disk)
			{
				++track;
				if (track == diskinfo[m_disktype].dir_track ||
						track == diskinfo[m_disktype].dir_track2)
				{
					++track;
				}
				if (track > diskinfo[m_disktype].last_track) tries = 0;
			}
			else
			{
				if (track == diskinfo[m_disktype].dir_track)
				{
					tries = 0;
				}
				else if (track < diskinfo[m_disktype].dir_track)
				{
					--track;
					if (track < diskinfo[m_disktype].first_track)
					{
						track = diskinfo[m_disktype].dir_track + 1;
						sector = 0;
						--tries;
						if (track > diskinfo[m_disktype].last_track) tries = 0;
					}
				}
				else // (track > diskinfo.dir_track)
				{
					++track;
					if (track == diskinfo[m_disktype].dir_track2) ++track;
					if (track > diskinfo[m_disktype].last_track)
					{
						track = diskinfo[m_disktype].dir_track - 1;
						sector = 0;
						--tries;
						if (track < diskinfo[m_disktype].first_track) tries = 0;
					}
				}
			}
		}
		if (!found &&
				tries == 0 &&
				track != diskinfo[m_disktype].dir_track &&
				diskinfo[m_disktype].data_to_dir_track)
		{
			track = diskinfo[m_disktype].dir_track;
			++tries;
		}
	}
	return found;
}

Diskimage::Direntry_state::Direntry_state() :
			track(0),
			sector(0),
			entry(0)
{
}

Diskimage::Direntry_state::Direntry_state(int track, int sector, int entry) :
			track(track),
			sector(sector),
			entry(entry)
{
}

Diskimage::Direntry* Diskimage::find_matching_direntry(
		bool (*matcher)(Diskimage::Direntry *d, int entry_index, void *userdata),
		void *userdata,
		Direntry_state *de_state )
{
	Direntry *found = NULL;

	Direntry_state des;

	if (de_state == NULL || de_state->track == 0)
	{
		// Start from the beginning
		des.track  = diskinfo[m_disktype].dir_track;
		des.sector = diskinfo[m_disktype].dir_sector;
		des.entry  = 0;
	}
	else // continue with processing
	{
		des = *de_state;
	}

	Direntry *dir_block = NULL;
	do
	{
		dir_block = (Direntry *)block(des.track, des.sector);
		for (; des.entry < 8 && !found; ++des.entry)
		{
			if (matcher(&dir_block[des.entry], des.entry, userdata))
			{
				found = &dir_block[des.entry];
			}
		}
		// Get next dir block if it exists
		if (des.entry >= 8 && dir_block[0].link_track != 0)
		{
			des.track  = dir_block[0].link_track;
			des.sector = dir_block[0].link_sector;
			des.entry = 0;
		}
	}
	while (des.entry < 8 && !found);

	// Exit state: next direntry to be processed
	// Search exhausted when des.entry == 8
	if (de_state) *de_state = des;

	return found;
}

static bool direntry_free_slot_matcher(Diskimage::Direntry *d, int, void *)
{
	if (!d)
		return false;

	if (d->filetype == FILE_DEL)
		return true;

	return false;
}

static bool direntry_name_matcher(Diskimage::Direntry *d, int, void *name)
{
	if (!d)
		return false;

	if (d->filetype == FILE_DEL)
		return false;

	std::vector<unsigned char>* ppetsciiname =
			(std::vector<unsigned char>*)name;

	if (match_name(*ppetsciiname, d->name))
	{
		return true;
	}
	return false;
}

static bool direntry_last_block_matcher(Diskimage::Direntry *d, int entry_index, void *)
{
	if (!d)
		return false;

	if (entry_index == 0 && d->link_track == 0 && d->link_sector == 0xff)
		return true;

	return false;
}

bool Diskimage::opendir( Dirstate& dirstate )
{
	std::copy(m_disk_block->name_id, m_disk_block->name_id+27, dirstate.name_id);
	int free = blocks_free();
	dirstate.free_lo = free % 256;
	dirstate.free_hi = free / 256;
	dirstate.de_state.track  = diskinfo[m_disktype].dir_track;
	dirstate.de_state.sector = diskinfo[m_disktype].dir_sector;
	dirstate.de_state.entry = 0;
	return true;
}

bool Diskimage::readdir( Dirstate& dirstate, Direntry& direntry )
{
	Direntry *dir_block =
			(Direntry *)block( dirstate.de_state.track, dirstate.de_state.sector );

	if (dirstate.de_state.entry >= 8)
	{
		if (dir_block[0].link_track == 0)
		{
			return false; // No more entries
		}
		dirstate.de_state.track  = dir_block[0].link_track;
		dirstate.de_state.sector = dir_block[0].link_sector;
		dirstate.de_state.entry  = 0;
		dir_block = (Direntry *)block( dirstate.de_state.track, dirstate.de_state.sector );
	}
	direntry = dir_block[dirstate.de_state.entry++];
	return true;
}

size_t Diskimage::read_file( std::vector<unsigned char>& data,
		std::vector<unsigned char>& petsciiname )
{
	Direntry *direntry = find_matching_direntry(direntry_name_matcher, &petsciiname, NULL);
	if (direntry == NULL)
	{
		std::string asciiname;
		petscii2ascii( petsciiname, asciiname );
		fprintf(stderr,"Could not open file '%s'\n",asciiname.c_str());
		throw raspbiec_error(IEC_FILE_NOT_FOUND);
	}

	int track  = direntry->first_track;
	int sector = direntry->first_sector;
	Dataentry *db = NULL;
	do
	{
		db = (Dataentry *)block(track,sector);
		track  = db->link_track;
		sector = db->link_sector;
		if (track != 0)
		{
			data.insert(data.end(), db->data, db->data + Dataentry::size);
		}
		else // Last block
		{
			data.insert(data.end(), db->data, db->data + (sector - offsetof(Dataentry, data)) + 1);
		}
	}
	while (track != 0);

	return data.size();
}

size_t Diskimage::write_file( std::vector<unsigned char>& data,
		std::vector<unsigned char>& petsciiname )
{
	// TODO: check for zero length data
	// Get first free direntry slot
	Direntry_state des;
	Direntry *direntry = find_matching_direntry(direntry_free_slot_matcher, NULL, &des);
	if (direntry == NULL)
	{
		// No space in the current directory blocks, get new
		Direntry_state last;
		direntry = find_matching_direntry(direntry_last_block_matcher, NULL, &last);
		if (find_next_free_block(last.track, last.sector, diskinfo[m_disktype].dir_interleave))
		{
			set_block_allocation(last.track, last.sector, true);
			direntry->link_track = last.track;
			direntry->link_sector = last.sector;
			unsigned char *new_dirblock = block(last.track, last.sector);
			std::fill(new_dirblock, new_dirblock + 256, 0x00);
			direntry = (Direntry *)new_dirblock;
			direntry->link_track = 0x00;
			direntry->link_sector = 0xff;
			des = last;
		}
	}

	if (direntry == NULL)
	{
		fprintf(stderr,"No space left in directory\n");
		throw raspbiec_error(IEC_NO_SPACE_LEFT_ON_DEVICE);
	}

	// Real 1541 will try to save and then abort if there is no space
	// We can check it beforehand
	int blocks = (data.size() + Dataentry::size-1)/Dataentry::size;
	if (blocks > blocks_free())
	{
		fprintf(stderr,"No space left on device\n");
		throw raspbiec_error(IEC_NO_SPACE_LEFT_ON_DEVICE);
	}

	int track  = diskinfo[m_disktype].dir_track - 1;
	int sector = 0;
	if (!find_next_free_block(track, sector, diskinfo[m_disktype].interleave))
	{
		fprintf(stderr,"No space left on device\n");
		throw raspbiec_error(IEC_NO_SPACE_LEFT_ON_DEVICE);
	}
	set_block_allocation(track, sector, true);

	// Clear the direntry in case it was used prevoiusly
	std::fill((unsigned char *)direntry, (unsigned char *)(direntry+1), 0x00);

	//TODO: proper filetypes
	direntry->filetype = FILE_PRG;
	direntry->first_track = track;
	direntry->first_sector = sector;
	std::fill(direntry->name, direntry->name + COUNT_OF(direntry->name), 0xA0);
	int name_len = std::min(petsciiname.size(), COUNT_OF(direntry->name));
	std::copy(petsciiname.begin(), petsciiname.begin() + name_len, direntry->name);

	int blocks_written = 0;
	std::vector<unsigned char>::iterator write_data = data.begin();

	for (;;)
	{
		Dataentry *datablock = (Dataentry *)block(track, sector);
		unsigned char *db = datablock->data;
		unsigned char *db_end = datablock->data + Dataentry::size;
		while (db != db_end && write_data != data.end())
		{
			*db++ = *write_data++;
		}
		++blocks_written;

		if (write_data != data.end()) // More data to be written, get a new block
		{
			if (!find_next_free_block(track, sector, diskinfo[m_disktype].interleave))
			{
				fprintf(stderr,"No space left on device\n");
				throw raspbiec_error(IEC_NO_SPACE_LEFT_ON_DEVICE);
			}
			set_block_allocation(track, sector, true);
			datablock->link_track = track;
			datablock->link_sector = sector;
		}
		else
		{
			datablock->link_track = 0;
			// The index of the last good data byte in the sector
			datablock->link_sector = offsetof(Dataentry, data) + (db - datablock->data) - 1;
			break;
		}
	}

	direntry->filetype |= FILE_CLOSED;
	direntry->size_hi = (blocks_written & 0xff00) >> 8;
	direntry->size_lo = blocks_written & 0x00ff;
	return 0;
}

int Diskimage::open_file(std::vector<unsigned char>& petsciiname)
{
	return 0;
}

bool Diskimage::close_file(int handle)
{
	return false;
}

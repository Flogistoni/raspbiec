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

#include <unistd.h>
#include <sys/stat.h>
#include <cctype>
#include <unordered_map>
#include "raspbiec_drive.h"
#include "raspbiec_exception.h"
#include "raspbiec_utils.h"

drive::drive(const int device_number, pipefd &bus, bool foreground) :
            m_dev(foreground),
			m_device_number(device_number),
			m_imagemode(false),
			m_foreground(foreground)
{
	m_dev.set_identity(device_number, bus);
}

drive::~drive()
{
}

void drive::serve(const char *path)
{
	struct stat stb;
	if(stat(path, &stb)==0)
	{
		m_imagemode = false;
		if (S_ISREG(stb.st_mode))
		{
			m_imagemode = true;
			m_img.open(path);
		}
		else if (!S_ISDIR(stb.st_mode))
		{
			fprintf(stderr, "'%s' is not a directory.\n", path);
			throw raspbiec_error(IEC_FILE_NOT_FOUND);
		}
	}
	else
	{
		fprintf(stderr, "Cannot access '%s'\n", path);
		throw raspbiec_error(IEC_FILE_NOT_FOUND);
	}
	if (!m_imagemode && chdir(path)==-1)
	{
		fprintf(stderr, "Cannot change to directory '%s'\n", path);
		throw raspbiec_error(IEC_FILE_NOT_FOUND);
	}

	reset_channels();

	printf("Entering disk drive service loop\n"
			"Exit with Ctrl-C or SIGINT\n");

	/* Service loop */
	raspbiec_sighandler::setup();
	device::Command cmd = device::Unknown;
	int command_byte = 0;
	do
	{
		try
		{
			int sa = -1;
			channel *pch = NULL;
			cmd = m_dev.receive_command(m_device_number, sa, command_byte);
			command_byte = 0;
			if (sa >= 0 && sa <=15)
			{
				pch = &channels[sa];
				pch->buscmd = cmd;
			}

			switch(cmd)
			{
			case device::Open:
			{
				printf("Open %d\n",sa);
				if (pch->open)
				{
					printf("Channel %d already open!\n", sa);
					throw raspbiec_error(IEC_ILLEGAL_STATE);
				}
				pch->open = true;
				receive_name_or_command(*pch);
				if (sa == 15)
				{
					// TODO DOS command
				}
				open_file(*pch);
				break;
			}
			case device::Close:
			{
				printf("Close %d\n",sa);
				if (!pch->open)
				{
					printf("Channel %d already closed!\n", sa);
					throw raspbiec_error(IEC_ILLEGAL_STATE);
				}
				pch->open = false;
				close_file(*pch);
				reset_channel(*pch);
				// TODO flush diskimage?
				break;
			}
			case device::Receive: // data from bus between LISTEN-UNLISTEN
			{
				if (!pch->open)
				{
					printf("Channel %d not open!\n", sa);
					throw raspbiec_error(IEC_ILLEGAL_STATE);
				}
				if (sa == 1)
				{
					printf("Save \"%s\"\n",pch->ascii.c_str());
					m_dev.receive_from_bus_verbose(back_inserter(pch->data));
					write_to_disk(*pch);
				}
				else if (sa >= 2 && sa <= 14)
				{
					printf("Write %d:\"%s\"\n",sa,pch->ascii.c_str());
					m_dev.receive_from_bus_verbose(back_inserter(pch->data));
					// TODO data write according to file type
				}
				else if (sa == 15)
				{
					receive_name_or_command(*pch);
					// TODO DOS command
				}
				break;
			}
			case device::Send: // data to bus between TALK-UNTALK
			{
				if (!pch->open)
				{
					printf("Channel %d not open!\n", sa);
					throw raspbiec_error(IEC_ILLEGAL_STATE);
				}
				if (sa == 0)
				{
					printf("Load \"%s\"\n",pch->ascii.c_str());
					read_from_disk(*pch);
					databuf_iter sent = m_dev.send_to_bus_verbose(pch->data.begin(), pch->data.end());
					if (sent != pch->data.end())
					{
						printf("?break\n");
					}
					pch->data.erase(pch->data.begin(), sent);
				}
				else if (sa >= 2 && sa <= 14)
				{
					printf("Read %d:\"%s\"\n",sa,pch->ascii.c_str());
					// TODO data read according to file type
					databuf_iter sent = m_dev.send_to_bus_verbose(pch->data.begin(), pch->data.end());
					pch->data.erase(pch->data.begin(), sent);
				}
				else if (sa == 15)
				{
					// TODO error channel read
				}
				break;
			}
			case device::Unlisten:
				printf("Unlisten\n");
				break;
			case device::Untalk:
				printf("Untalk\n");
				break;
			case device::Exit:
				printf("\nExiting disk drive service loop\n");
				break;
			case device::OpenOtherDevice:
				printf("Open other device\n");
				break;
			case device::CloseOtherDevice:
				printf("Close other device\n");
				break;
			case device::ReceiveOtherDevice:
				printf("Receive other device\n");
				break;
			case device::SendOtherDevice:
				printf("Send other device\n");
				break;
			case device::Unknown:
				// Ignore, usually results from spurious ATN asserts
				// e.g.during power cycling
				break;
			default:
				printf("Unknown command %d\n", cmd);
				break;
			}
		}
		catch( raspbiec_error &e )
		{
			int status = e.status();
			if (status == IEC_ILLEGAL_STATE)
				throw;

			if (status <= IEC_COMMAND_RANGE_START &&
				status >= IEC_COMMAND_RANGE_END)
			{
				printf("\nUnexpected command %d\n",status);
				command_byte = status;
			}
			else
			{
				// Continue serving in other cases
				printf("\n%s\n",e.what());
				m_dev.clear_error();
				//reset_channels();
			}
		}
	}
	while( cmd != device::Exit );

	if (m_imagemode)
	{
		m_img.close();
		m_imagemode = false;
	}
}

void drive::reset_channel(channel &ch)
{
	ch.open = false;
	ch.petscii.clear();
	ch.ascii.clear();
	ch.data.clear();
	ch.fd = -1;
}

void drive::reset_channels()
{
	for (int i=0; i<16; ++i)
	{
		reset_channel(channels[i]);
		channels[i].number = i;
	}
}

void drive::open_file(channel &ch)
{
	if (ch.number <= 14 && ch.ascii != "$")
	{
		if (m_imagemode)
			ch.fd = m_img.open_file(ch.petscii);
		else
			ch.fd = open_local_file(ch.ascii.c_str(), "r"); // todo: mode
	}
}

void drive::close_file(channel &ch)
{
	if (ch.number <= 14 && ch.ascii != "$")
	{
		if (m_imagemode)
			m_img.close_file(ch.fd);
		else
			close_local_file(ch.fd);
	}
}

void drive::read_from_disk(channel &ch)
{
	if (ch.ascii == "$")
	{
		if (m_imagemode)
			read_diskimage_dir(ch.data, m_img, m_foreground);
		else
			read_local_dir(ch.data, ".", m_foreground);
	}
	else /* ordinary PRG file */
	{
		if (m_imagemode)
			m_img.read_file(ch.data, ch.name);
		else
			read_local_file(ch.data, ch.ascii.c_str());
	}
}

void drive::write_to_disk(channel &ch)
{
	if (m_imagemode)
		m_img.write_file(ch.data, ch.name);
	else
		write_local_file(ch.data, ch.ascii.c_str());
}

static int findchar(unsigned char c,
		std::vector<unsigned char> vec)
{
	for (unsigned int i=0; i < vec.size(); ++i)
	{
		if (c == vec[i]) return i;
	}
	return -1;
}

// Return 0 when successful
// Return <0 otherwise (-errorcode)
int drive::determine_command(channel &ch)
{
	ch.usrcmd = UC_NONE;
	size_t len = ch.petscii.size();
	if (len > 1)
	{
		// Remove tailing CR or CR+LF
		if (ch.petscii[len-1] == PETSCII_CR)
		{
			ch.petscii.erase(ch.petscii.end() - 1);
			--len;
		}
		else if (ch.petscii[len-2] == PETSCII_CR)
		{
			ch.petscii.erase(ch.petscii.end() - 2, ch.petscii.end());
			len -= 2;
		}
	}
	if (len > 0)
	{
		switch (ch.petscii[0])
		{
		case PETSCII_n:
			ch.usrcmd = UC_NEW;
			break;
		case PETSCII_s:
			ch.usrcmd = UC_SCRATCH;
			break;
		case PETSCII_r:
			ch.usrcmd = UC_RENAME;
			break;
		case PETSCII_c:
			ch.usrcmd = UC_COPY;
			break;
		case PETSCII_ET:
			ch.usrcmd = UC_UTIL_LDR;
			break;
		case PETSCII_p:
			ch.usrcmd = UC_POSITION;
			break;
		case PETSCII_u:
			ch.usrcmd = UC_USER;
			break;
		case PETSCII_b:
		{
			unsigned int pos = findchar(PETSCII_MINUS, ch.petscii);
			if (pos >= 0 && pos+1 < len)
			{
				// Found '-' and there is a character after it
				switch (ch.petscii[pos+1])
				{
				case PETSCII_a:
					ch.usrcmd = UC_BLOCK_ALLOCATE;
					break;
				case PETSCII_f:
					ch.usrcmd = UC_BLOCK_FREE;
					break;
				case PETSCII_r:
					ch.usrcmd = UC_BLOCK_READ;
					break;
				case PETSCII_w:
					ch.usrcmd = UC_BLOCK_WRITE;
					break;
				case PETSCII_e:
					ch.usrcmd = UC_BLOCK_EXECUTE;
					break;
				case PETSCII_p:
					ch.usrcmd = UC_BUFFER_POINTER;
					break;
				default:
					return -31;
				}
			}
			else
			{
				return -31;
			}
		}
		break;
		case PETSCII_m:
		{
			if (len >= 3 && ch.petscii[1] == PETSCII_MINUS)
			{
				switch (ch.petscii[2])
				{
				case PETSCII_r:
					ch.usrcmd = UC_MEMORY_READ;
					break;
				case PETSCII_w:
					ch.usrcmd = UC_MEMORY_WRITE;
					break;
				case PETSCII_e:
					ch.usrcmd = UC_MEMORY_EXECUTE;
					break;
				default:
					return -31;
				}
			}
			else
			{
				return -31;
			}
		}
		break;
		case PETSCII_d:
			ch.usrcmd = UC_DUPLICATE;
			break;
		case PETSCII_i:
			ch.usrcmd = UC_INITIALIZE;
			break;
		case PETSCII_v:
			ch.usrcmd = UC_VALIDATE;
			break;
		default:
			return -31;
		}
	}
	return 0;
}

struct usercommand_t
{
	usercommand command;
	char *pattern;
};

// Syntax:
// '' = literal, x = any character
// [] = optional
// * = any number of any characters until next expected literal is encountered
// ? = any one character, <> = metainfo
// + = repeat 1 to inf times, {m,n} = repeat m to n times
// , = numbers separated by blank/comma/cursor right
// expected character strings: $stringname, #numbername, @bytevalue
// d = drive number: '0' -> 0, '1' -> 1, any other char -> 0
static const usercommand_t usercommand_table[] =
{
// C,R,S,N,Open: no commas before 1st colon = if the first file does not have
// drive number specified, the others cannot have either
//
// C "COPY:newfile=oldfile"
// 'c'*[d]':'<name>'='[[d]':']<name>(','[[d]':']<name>){0,3}
	{ UC_COPY,			 "'c'*[d]':'$newname'='[[d]':']$oldname1][','[[d]':']$oldname2][','[[d]':']$oldname3][','[[d]':']$oldname4]" },
// Syntax pattern: filestream 1 - exactly 1 name, no wildcards
//                 filestream 2 - 1-4 names, no wildcards

// R "RENAME:newname=oldname"
// 'r'*[d]':'<name>'='[[d]':']<name>
	{ UC_RENAME,		 "'r'*[d]':'$newname'='[[d]':']$oldname" },
// Syntax pattern: filestream 1 - exactly 1 name, no wildcards
//                 filestream 2 - exactly 1 name, no wildcards

// S "SCRATCH:name"
// 's'*[d]':'<name>(','[[d]':']<name>){0,4}['='<type>]
	{ UC_SCRATCH,		 "'s'*[d]':'$name1[','[[d]':']$name2][','[[d]':']$name3][','[[d]':']$name4][','[[d]':']$name5]['='$type]" },
// Syntax pattern: filestream 1 - 1-5 names
//                 filestream 2 - optional filetype, no wildcards

// N "NEW:name,id" - format
// 'n'*[d]':'<name>[,<id>]
	{ UC_NEW,			 "'n'*[d]':'$name[,$id]" },
// Syntax pattern: filestream 1 - 1-2 names (additionals ignored), no wildcards
// Syntax pattern: filestream 2 - optional name, no wildcards (ignored)

// 'u'<char>
// U0 - restore user jump table pointer at $6B
// The remaining Ux commands work as follows:
// - Get the byte value of the PETSCII character following the U, subtract 1, bitwise-and with 0x0F
// - With that result, index the 16-bit address table pointed to by the vector at $6B and jump to that address
// Using only the 4 lowest bits of the second character results in the alternate versions of the commands
// U1/UA "U1:"<channel><drive><track><sector> B-R w/o changing buffer pointer
// U2/UB "U2:"<channel><drive><track><sector> B-W w/o changing buffer pointer
// U3/UC - jump to 0x0500
// U4/UD - jump to 0x0503
// U5/UE - jump to 0x0506
// U6/UF - jump to 0x0509
// U7/UG - jump to 0x050C
// U8/UH - jump to 0x050F
// U9/UI - jump to (0x0065) Pointer to start of NMI routine $EB22 "U9 is an alternate reset that bypasses the power-on diagonstics."
// U;/UJ - power-up vector "The issuance of a UJ command is supposed to reset the 1541. Instead, it hangs the 1541."
// UI+   - Set C64 speed
// UI-   - set VIC-20 speed
	{ UC_USER,		 "'u'@user[?#channel,#drive,#track,#sector]" },

// ['b-'x|'b'*'-'x*':']<PETSCII numbers separated by blank/comma/cursor right>
	{ UC_BLOCK_READ,	 "'b'['-r'|*'-r'*':']#channel,#drive,#track,#sector" },
	{ UC_BLOCK_WRITE,    "'b'['-w'|*'-w'*':']#channel,#drive,#track,#sector" },
	{ UC_BLOCK_ALLOCATE, "'b'['-a'|*'-a'*':']#drive,#track,#sector" },
	{ UC_BLOCK_FREE,     "'b'['-f'|*'-f'*':']#drive,#track,#sector" },
	{ UC_BUFFER_POINTER, "'b'['-p'|*'-p'*':']#channel,#location" },
	{ UC_BLOCK_EXECUTE,	 "'b'['-e'|*'-e'*':']#channel,#drive,#track,#sector" },

// 'm-'x<binary data>
	{ UC_MEMORY_READ,	 "'m-r'@address_lo@address_hi[@num_bytes]" },
	{ UC_MEMORY_WRITE,	 "'m-w'@address_lo@address_hi@num_bytes@data_bytes+" }, // num_bytes = 1..34
	{ UC_MEMORY_EXECUTE, "'m-e'@address_lo@address_hi" },

	{ UC_DUPLICATE, 	 "'d'*" },
	{ UC_INITIALIZE, 	 "'i'*[d][':']" },
	{ UC_VALIDATE, 		 "'v'*[d][':']" },

// FORMAT FOR THE RECORD# COMMAND:
// PRINT#15, "P" + CHR$ (channel # + 96) + CHR$ (<record #) + CHR$(>record #) + CHR$ (offset)
// where "channel #" is the channel number specified in the current Open statement for the
// specified file, "<record #" is the low byte of the desired record number, expressed as a
// two byte integer, ">record #" is the high byte of the desired record number, and an
// optional "offset" value, if present, is the byte within the record at which a following
// Read or Write should begin.
// "P"<binary data>
	{ UC_POSITION,		 "'p'@channel@record_lo@record_hi@offset" },

// Utility loader
// Normal entry:
// The disk command "&filename" will load and execute the file whose filename is specified. For example:
// PRINT# 15,"&DISK TASK"
// File structure:
// The utility or program must be of the following form.
// 		File type: USR
// 		Bytes 1/2:	Load address in disk RAM (lo/hi).
// 		Byte 3:		Lo byte of the length of the routine
// 		Bytes 4/N:	Disk routine machinecode.
// 		Byte N+1:	Checksum. Note that the checksum includes all bytes including the load address,
// 					formula: CHECKSUM = CHECKSUM + BYTE + CARRY
// NOTE: Routines may be longer than 256 bytes. However, there MUST be a
// 		 valid checksum byte after the number specified in byte #3 and after each subsequent 256 bytes!
//
// The name on the disk must actually be '&<name>' due to what I think is a parsing bug
// Also for the same reason anything after the first comma is ignored ('&' + first name of the filestream is used)
	{ UC_UTIL_LDR,		 "'&'$name" },

// Table end marker
	{ UC_NONE, 			 "" }
}

// Opening a file:
// *[[d]':']<name>[','<type>][','<mode>]
// *[[d]':']<name>',L'*','<record length byte> in case of REL file
// '@'*[[d]':']<name>[,<type>][,<mode>] when writing/saving
// Note: in normal file open '=' has no special meaning
//
// '*'*, secondary==0 "load the last referenced program".
// Check by loading the last program's track link from PRGTRK ($7E).
// If 0, there is no last program -> init drive & load normally
//
// '$'[[d]':'][<name>](','[[d]':']<name>){0,4}['='<type>]
// secondary==0: create dir as BASIC listing, !=0: raw dir as SEQ file
//
// Direct access channel
// '#' get first available channel
// '#'['0'|'1'|'2'|'3'|'4']
// ("#"[<numbers separated by blank/comma/cursor right>], but only the lowbyte of 1st number is used and it must be <=5)
// Direct access channel is used for transferring data needed by these commands
// 		Block-Read (U1)
// 		Buffer-Pointer (B-P)
// 		Block-Write (U2)
// 		Memory-Read (M-R)
// 		Memory-Write (M-W)
// 		Block-Allocate (B-A)
// 		Block-Free (B-F)
// 		Memory-Execute (M-E)
// 		Block-Execute (B-E)
// open a,b,c,"#" : print/get/input : close a

// File control:
// R - read
// W - write
// A -
// M -
// Type:
// D - DEL
// S - SEQ
// P - PRG
// U - USR
// L - REL
// Special filenames:
// '*'
// '$'
// Commands:
// $ "$0:name" - directory with wildcards
// @ "@0:name" - save and replace
// 0 "0:name,type,direction"
// # "#" or "#[number]" - random access data channel
// Commands on channel 15, 1st letter
// V,I,D,M,B,U,P,&,C,R,S,N
// error 31 if not found
// R,S,N need ':' otherwise error 34

enum parsestate_t
{
	ps_none,
	ps_begin,
	ps_end,
	ps_literal,
	ps_string,
	ps_number,
	ps_byte,
	ps_star,
	ps_anychar,
	ps_repeat,
	ps_drivenum,
	ps_unchanged
};

enum optionstate_t
{
	os_no_option,
	os_in_option,
	os_skip_this_option,
	os_skip_rest_of_options,
	os_option_found,
	os_unchanged
};
// TODO: nested options

int drive::parse_command(channel &ch)
{
	ch.usrcmd = UC_NONE;
	size_t len = ch.petscii.size();
	if (len > 1)
	{
		// Remove tailing CR or CR+LF
		if (ch.petscii[len - 1] == PETSCII_CR)
		{
			ch.petscii.erase(ch.petscii.end() - 1);
			--len;
		}
		else if (ch.petscii[len - 2] == PETSCII_CR)
		{
			ch.petscii.erase(ch.petscii.end() - 2, ch.petscii.end());
			len -= 2;
		}
	}

	std::unordered_map<std::string, std::string> tokenmap;
	int pattern_index = 0;
	std::vector<unsigned char>::iterator cmditer = ch.petscii.begin();
	bool match = false;
	do
	{
		// Check every pattern one by one. In practice, every command
		// will be recognized from the first literals, thus rejection
		// is very quick
		const usercommand_t& uc = usercommand_table[pattern_index++];
		if (uc.command == UC_NONE) break;
		std::string str;
		parsestate_t parsestate = ps_begin;
		optionstate_t optionstate = os_no_option;
		match = true;
		for (const char *pch = uc.pattern; match; ++pch)
		{
			char ch = *pch;
			// Make sure that inside a literal only the end marker is interpreted
			if (parsestate == ps_literal && ch != '\'') // ??Quoted end marker handling needed?
			{
				str.push_back(ch);
				continue;
			}

			parsestate_t new_parsestate = ps_unchanged;
			optionstate_t new_optionstate = os_unchanged;
			bool token_end = false;
			switch (ch)
			{
			case '\'': new_parsestate = (parsestate == ps_literal) ? ps_end : ps_literal; break;
			case '$': new_parsestate = ps_string;		break;
			case '#': new_parsestate = ps_number;		break;
			case '@': new_parsestate = ps_byte;			break;
			case '[': new_optionstate = os_in_option;   break;
			case '|': if (optionstate != os_skip_rest_of_options) new_optionstate = os_in_option; break;
			case ']': new_optionstate = os_no_option;	break;
			case '*': new_parsestate = ps_star;			break;
			case '?': new_parsestate = ps_anychar;		break;
			case '+': new_parsestate = ps_repeat;		break;
			case 'd': if (parsestate == ps_end) new_parsestate = ps_drivenum; break;
			case '\0': new_parsestate = ps_end;		break;
			}

			if (new_parsestate != ps_unchanged)
			{
				parsestate = new_parsestate;
				token_end = true;
			}
			if (new_optionstate != os_unchanged)
			{
				optionstate = new_optionstate;
				token_end = true;
			}
			if (optionstate == os_skip_rest_of_options ||
				optionstate == os_skip_this_option)
			{
				continue;
			}

			if (!token_end)
			{
				str.push_back(ch);
				continue;
			}
			else
			{
				parsestate = ps_end;
			}

			// This is reached only when a token has been completed
			// The token itself is contained in str
			bool submatch = true;
			std::vector<unsigned char> petstr; // Part of the command string matching the token
			switch (parsestate)
			{
			case ps_literal:
			{
				if (str.size() > ch.petscii.end() - cmditer)
				{
					submatch = false; // Literal longer than the remaining buffer
				}
				else
				{
					std::pair<std::vector<unsigned char>::iterator, std::vector<unsigned char>::iterator> matchedstr;
					ascii2petscii(str, petstr);
					matchedstr = std::mismatch(petstr.begin(), petstr.end(), cmditer);
					if (matchedstr.second != cmditer)
					{
						cmditer = matchedstr.second; // Consume matched portion
					}
					else
					{
						submatch = false;
					}
				}
				break;
			}
			case ps_string:
				cmditer = petsciialnum(cmditer, ch.petscii.end(), petstr);
				tokenmap[str] = petstr;
				break;
			case ps_number:
				cmditer = petsciinum(cmditer, ch.petscii.end(), petstr);
				tokenmap[str] = petstr;
				break;
			case ps_byte:
				petstr.push_back(*cmditer++);
				tokenmap[str] = petstr;
				break;
			case ps_anychar:
				*cmditer++;
				break;
			default:
				break;
			}

			if (submatch)
			{
				if (optionstate == os_in_option) optionstate = os_skip_rest_of_options;
			}
			else // no submatch
			{
				if (optionstate == os_in_option) optionstate = os_skip_this_option; // Try the next option
				else if (optionstate == os_no_option) match = false; // This was the only or last option
			}

			str.clear();
			if (ch == '\0') break;
		}
	}
	while (!match)

	return 0;
}

void parse(drive::channel &ch)
{

	ch.name.clear();
	ch.command.clear();
	ch.rwam    = PETSCII_r;
	ch.type    = PETSCII_SPC;

	int commas = 0;
	int colons = 0;
	int semicolons = 0;
	for (databuf_iter it = ch.petscii.begin(); it != ch.petscii.end(); ++it)
	{
		switch(*it)
		{
		case  PETSCII_COLON:
			++colons;
			ch.name.clear();
			break;

		case PETSCII_COMMA: // ','
		++commas;
		break;

		default:
			if (semicolons == 0)
				ch.command.push_back(*it);

			if (commas == 0)
				ch.name.push_back(*it);
			else if (commas == 1)
				ch.rwam = *it;
			else if (commas == 2)
				ch.type = *it;
			break;
		}
	}
	if (semicolons == 0)
		ch.command.clear();
}

void drive::receive_name_or_command(drive::channel &ch)
{
	ch.petscii.clear();
	m_dev.receive_from_bus(back_inserter(ch.petscii), 0);
	petscii2ascii( ch.petscii, ch.ascii );
	if (ch.number == 15)
		printf("command \"%s\"\n",ch.ascii.c_str());
	else
		printf("filename \"%s\"\n",ch.ascii.c_str());

	// Command is either OPEN on ch15 or PRINT#
	if (ch.number == 15 ||
		ch.buscmd == device::Receive)
	{
		int err = determine_command(ch);
		if (err == 0)
		{
			err = execute_command(ch);
		}

		if (err < 0)
		{
			//return_error_code(err);
		}
	}
}

int drive::execute_command(channel &ch)
{
	return 0;
}

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

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <iterator>
#include "raspbiec.h"
#include "raspbiec_utils.h"
#include "raspbiec_exception.h"
#include "raspbiec_drive.h"

// How to allocate the processes/threads when processing disk image command,
// i.e. does computer or drive portion get the foreground
// (== debug prints and debugger breakpoints)
static const bool foreground_drive = true;

enum raspbiec_mode {
	MODE_NONE,
	MODE_SERVE,
	MODE_LOAD,
	MODE_SAVE,
	MODE_COMMAND,
	MODE_ERROR_CHANNEL
};

raspbiec_mode determine_mode(const char *s);

int main(int argc, char** argv)
{
	if (argc < 2)
	{
		char *basec = strdup(argv[0]);
		char *bname = basename(basec);
		printf("As drive:    %s [serve] <directory or disk image> [<command>|<device #>]\n", bname);
		printf("              <command> is a computer command below applied to the disk image\n");
		printf("As computer: %s load <filename> [<device #>]\n", bname);
		printf("             %s save <filename> [<device #>]\n", bname);
		printf("             %s cmd <command> [<device #>]\n", bname);
		printf("             %s errch [<device #>]\n", bname);
		free(basec);
		return EXIT_SUCCESS;
	}

	const char *string = NULL;
	const char *dir_or_image = NULL;
	int devicenum;
	int an = 2;

	int primary_mode = determine_mode(argv[1]);
	int secondary_mode = MODE_NONE;

	int mode = primary_mode;
	bool handing_secondary_mode = false;
	do
	{
		switch(mode)
		{
		case MODE_LOAD:
			string    = (an < argc) ? argv[an] : NULL;
			devicenum = (an+1 < argc) ? strtol(argv[an+1], NULL, 10) : 8;
            if (!string) fprintf(stderr,"Missing filename for load\n");
			break;

		case MODE_SAVE:
			string    = (an < argc) ? argv[an] : NULL;
			devicenum = (an+1 < argc) ? strtol(argv[an+1], NULL, 10) : 8;
            if (!string) fprintf(stderr,"Missing filename for save\n");
			break;

		case MODE_COMMAND:
			string    = (an < argc) ? argv[an] : NULL;
			devicenum = (an+1 < argc) ? strtol(argv[an+1], NULL, 10) : 8;
            if (!string) fprintf(stderr,"Missing command\n");
			break;

		case MODE_ERROR_CHANNEL:
			string    = "";
			devicenum = (an < argc) ? strtol(argv[an], NULL, 10) : 8;
			break;

        case MODE_NONE:
            primary_mode = MODE_SERVE;
			--an; // argv[1] was not a reserved word for mode
		case MODE_SERVE:
			dir_or_image = (an < argc) ? argv[an] : ".";
			secondary_mode = (an+1 < argc) ? determine_mode(argv[an+1]) : MODE_NONE;
			if (!handing_secondary_mode && secondary_mode != MODE_NONE)
			{
				// command for diskimage was found
				an = an+2;
				mode = secondary_mode;
				handing_secondary_mode = true;
				continue;
			}
			devicenum = (an+1 < argc) ? strtol(argv[an+1], NULL, 10) : 8;
			break;
		}
		handing_secondary_mode = false;
	}
	while (handing_secondary_mode);

	if (!string && !(primary_mode == MODE_SERVE && secondary_mode == MODE_NONE))
	{
		return EXIT_FAILURE;
	}

	try
	{
		pipefd communication_bus;
		bool wait_for_child = false;
		bool foreground = true;

		mode = primary_mode;
		if (primary_mode == MODE_SERVE && secondary_mode != MODE_NONE)
		{
			// Diskimage operation, fork drive and computer parts to separate processes
			communication_bus.open_pipe();
			pid_t cpid = fork();
			if (cpid == -1)
			{
				throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);
			}
			else if ((!foreground_drive && cpid == 0) || /* Drive process (child) */
                     (foreground_drive && cpid != 0))    /* Drive process (parent) */
			{
				wait_for_child = foreground_drive;
				foreground = foreground_drive;
				communication_bus.set_direction_B_to_A();
				drive virtual_c1541(devicenum, communication_bus, foreground);
				virtual_c1541.serve(dir_or_image);
			}
			else /* Computer process */
			{
				wait_for_child = !foreground_drive;
				foreground = !foreground_drive;
				communication_bus.set_direction_A_to_B();
				mode = secondary_mode;
			}
		}
		else
		{
			communication_bus.open_dev(); // actual IEC bus
		}

		switch(mode)
		{
		case MODE_SERVE: // Normal service to IEC bus
		{
			drive c1541(devicenum, communication_bus, foreground);
			c1541.serve(dir_or_image);
			break;
		}
		case MODE_LOAD:
		{
			computer c64(communication_bus, foreground);
			c64.load(string, devicenum);
			break;
		}
		case MODE_SAVE:
		{
			computer c64(communication_bus, foreground);
			c64.save(string, devicenum);
			break;
		}
		case MODE_COMMAND:
		{
			computer c64(communication_bus, foreground);
			c64.command(string, devicenum);
			break;
		}
		case MODE_ERROR_CHANNEL:
		{
			computer c64(communication_bus, foreground);
			c64.read_error_channel(devicenum);
			break;
		}
		default:
			throw raspbiec_error(IEC_UNKNOWN_MODE);
			break;
		}

		if (wait_for_child)
		{
	        wait(NULL);
		}
	}
	catch (raspbiec_error &e)
	{
		printf("%s\n",e.what());
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

raspbiec_mode determine_mode(const char *s)
{
	if (s == NULL)
	{
		return MODE_NONE;
	}
	else if (strcmp("load",s) == 0)
	{
		return MODE_LOAD;
	}
	else if (strcmp("save",s) == 0)
	{
		return MODE_SAVE;
	}
	else if (strcmp("cmd",s) == 0)
	{
		return MODE_COMMAND;
	}
	else if (strcmp("errch",s) == 0)
	{
		return MODE_ERROR_CHANNEL;
	}
	else if (strcmp("serve",s) == 0)
	{
		return MODE_SERVE;
	}
	// default
	return MODE_NONE;
}

/*********************************************************************/

computer::computer(pipefd &bus, const bool foreground) :
    m_dev(foreground)
{
	m_dev.set_identity(device::computer, bus);
}

computer::~computer()
{
}

void computer::load(const char* filename, int device_number)
{
	bool is_directory( strcmp(filename,"$")==0 );

	if (!is_directory && local_file_exists(filename))
	{
		printf("Not overwriting '%s'\n", filename);
		throw raspbiec_error(IEC_FILE_EXISTS);
	}

	databuf_t ram;

	try
	{
		m_dev.load(back_inserter(ram), filename, device_number, 1);
		printf("%ld bytes\n", ram.size());
	}
	catch (raspbiec_error &e)
	{
		printf("%s\n",e.what());
		read_error_channel(device_number);
		return;
	}

	if (is_directory)
	{
		if (m_foreground) basic_listing(ram);
	}
	else
	{
		write_local_file(ram, filename);
	}
}

void computer::save(const char *filename, int device_number)
{
	databuf_t prg;
	try
	{
		const_databuf_iter save_end = m_dev.save(prg.begin(), prg.end(), filename, device_number, 0);
		printf("%ld bytes\n", save_end - prg.begin());
	}
	catch (raspbiec_error &e)
	{
		printf("%s\n", e.what());
		read_error_channel(device_number);
	}
}

void computer::command(const char *command, int device_number)
{
	std::string asccmd(command);
	std::vector<unsigned char> cmd;
	ascii2petscii( asccmd, cmd );
	m_dev.send_data( cmd.begin(), cmd.end(), device_number, 15 );
	read_error_channel(device_number);
}

void computer::read_error_channel(int device_number)
{
	std::vector<unsigned char> msg;
	m_dev.receive_data(back_inserter(msg), device_number, 15);

	std::string ascmsg;
	petscii2ascii( msg, ascmsg );
	printf("%s\n", ascmsg.c_str());
}

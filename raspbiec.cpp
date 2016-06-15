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

#include "raspbiec.h"
#include "raspbiec_utils.h"
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

enum raspiec_mode {
    MODE_NONE,
    MODE_SERVE,
    MODE_LOAD,
    MODE_SAVE,
    MODE_COMMAND,
    MODE_ERROR_CHANNEL
};

int main(int argc, char** argv)
{
    int mode = MODE_NONE;
    int argno = 2;
    const char *path = NULL;
    int devicenum;

    if (argc < 2)
    {
	char *basec = strdup(argv[0]);
	char *bname = basename(basec);
	printf("As drive:    %s [serve] <directory> [<device #>]\n", bname);
	printf("As computer: %s load <filename> [<device #>]\n", bname);
	printf("             %s save <filename> [<device #>]\n", bname);
	printf("             %s cmd <command> [<device #>]\n", bname);
	printf("             %s errch [<device #>]\n", bname);
	free(basec);
	return EXIT_SUCCESS;
    }
    else if (strcmp("load",argv[1]) == 0)
    {
	mode = MODE_LOAD;
	path      = (2 < argc) ? argv[2] : NULL;
	devicenum = (3 < argc) ? strtol(argv[3], NULL, 10) : 8;
    }
    else if (strcmp("save",argv[1]) == 0)
    {
	mode = MODE_SAVE;
	path      = (2 < argc) ? argv[2] : NULL;
	devicenum = (3 < argc) ? strtol(argv[3], NULL, 10) : 8;
    }
    else if (strcmp("cmd",argv[1]) == 0)
    {
	mode = MODE_COMMAND;
	path      = (2 < argc) ? argv[2] : NULL;
	devicenum = (3 < argc) ? strtol(argv[3], NULL, 10) : 8;
    }
    else if (strcmp("errch",argv[1]) == 0)
    {
	mode = MODE_ERROR_CHANNEL;
	path = "";
	devicenum = (2 < argc) ? strtol(argv[2], NULL, 10) : 8;
    }
    else if (strcmp("serve",argv[1]) == 0)
    {
	mode = MODE_SERVE;
	path      = (2 < argc) ? argv[2] : ".";
	devicenum = (3 < argc) ? strtol(argv[3], NULL, 10) : 8;
    }
    else
    {
	mode = MODE_SERVE;
	path      = (1 < argc) ? argv[1] : ".";
	devicenum = (2 < argc) ? strtol(argv[2], NULL, 10) : 8;
    }

    if (!path)
    {
	fprintf(stderr,"Missing filename\n");
	return EXIT_FAILURE;
    }

    try
    {
	device iecbus;

	switch(mode)
	{
	case MODE_SERVE:
	{
	    drive c1541(iecbus, devicenum);
	    c1541.serve(path);
	    break;
	}
	case MODE_LOAD:
	{
	    computer c64(iecbus);
	    c64.load(path, devicenum);
	    break;
	}
	case MODE_SAVE:
	{
	    computer c64(iecbus);
	    c64.save(path, devicenum);
	    break;
	}
	case MODE_COMMAND:
	{
	    computer c64(iecbus);
	    c64.command(path, devicenum);
	    break;
	}
	case MODE_ERROR_CHANNEL:
	{
	    computer c64(iecbus);
	    c64.read_error_channel(devicenum);
	    break;
	}
	default:
	    throw raspbiec_error(IEC_UNKNOWN_MODE);
	    break;
	}
    }
    catch (raspbiec_error &e)
    {
	printf("%s\n",e.what());
	return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/*********************************************************************/

drive::drive(device &dev, int device_number) :
    m_dev(dev),
    m_device_number(device_number)
{
    m_dev.set_identity(device_number);
}

drive::~drive()
{
}

void drive::serve(const char *path)
{
    struct stat stb;
    if(stat(path, &stb)==0)
    {
	if (S_ISREG(stb.st_mode))
	{
	    fprintf(stderr,"'%s' is a regular file, expected a directory.\n"
		    "(Disk images not yet supported)\n", path);
	    throw raspbiec_error(IEC_GENERAL_ERROR);
	}
	if (!S_ISDIR(stb.st_mode))
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
    if (chdir(path)==-1)
    {
	fprintf(stderr, "Cannot change to directory '%s'\n", path);
	throw raspbiec_error(IEC_FILE_NOT_FOUND);
    }
    
    for (int i=0; i<16; ++i)
    {
	channels[i].petscii_name.clear();
	channels[i].ascii_name.clear();
	channels[i].open = false;
    }
    
    printf("Entering disk drive service loop\n"
	   "Exit with Ctrl-C or SIGINT\n");
    
    /* Service loop */
    raspbiec_sighandler::setup();
    device::Command cmd = device::Unknown;
    do
    {
	try {
	    int sa = -1;
	    cmd = m_dev.receive_command(m_device_number, sa);
	    switch(cmd)
	    {
	    case device::Open:
	    {
		printf("Open %d\n",sa);
		channel &ch = channels[sa];
		if (ch.open)
		{
		    printf("Channel %d aready open!\n", sa);
		    throw raspbiec_error(IEC_ILLEGAL_STATE);
		}
		receive_filename(ch);
		ch.open = true;
		break;
	    }	
	    case device::Close:
	    {
		printf("Close %d\n",sa);
		channel &ch = channels[sa];
		if (!ch.open)
		{
		    printf("Channel %d aready closed!\n", sa);
		    throw raspbiec_error(IEC_ILLEGAL_STATE);
		}
		ch.petscii_name.clear();
		ch.ascii_name.clear();
		ch.open = false;
		break;
	    }	
	    case device::Save:
	    {
		channel &ch = channels[sa];
		printf("Save %d:\"%s\"\n",sa,ch.ascii_name.c_str());
		if (!ch.open)
		{
		    printf("Channel %d not open!\n", sa);
		    throw raspbiec_error(IEC_ILLEGAL_STATE);
		}
		save(ch);
		break;
	    }	
	    case device::Load:
	    {
		channel &ch = channels[sa];
		printf("Load %d:\"%s\"\n",sa,ch.ascii_name.c_str());
		if (!ch.open)
		{
		    printf("Channel %d not open!\n", sa);
		    throw raspbiec_error(IEC_ILLEGAL_STATE);
		}
		load(ch);
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
	    case device::SaveOtherDevice:
		printf("Save other device\n");
		break;
	    case device::LoadOtherDevice:
		printf("Load other device\n");
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
	    if (e.status() == IEC_ILLEGAL_STATE)
		throw;
	    // Continue serving in other cases
	    printf("\n%s\n",e.what());
	    m_dev.clear_error();
	    for (int i=0; i<16; ++i)
	    {
		channels[i].petscii_name.clear();
		channels[i].ascii_name.clear();
		channels[i].open = false;
	    }
	}
    }
    while( cmd != device::Exit );
}

void drive::receive_filename(channel &ch)
{
    ch.petscii_name.clear();
    m_dev.receive_from_bus(ch.petscii_name, 0);
    petscii2ascii( ch.petscii_name, ch.ascii_name );
    printf("filename \"%s\"\n",ch.ascii_name.c_str());
}

// TODO: Instead of reading and writing to a memory buffer
// some kind of a file stream object could also be used
// instead if interelaved I/O is desired

void drive::load(channel &ch)
{
    std::vector<unsigned char> buf;
    if (ch.ascii_name == "$")
    {
	read_local_dir(buf, ".");
    }
    else /* ordinary PRG file */
    {
	read_local_file(buf, ch.ascii_name.c_str());
    }
    m_dev.send_to_bus_verbose(buf);
}

void drive::save(channel &ch)
{
    std::vector<unsigned char> buf;
    m_dev.receive_from_bus_verbose(buf);
    write_local_file(buf, ch.ascii_name.c_str());
}

/*********************************************************************/

computer::computer(device &dev) :
    m_dev(dev)
{
    m_dev.set_identity(device::computer);
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

    std::vector<unsigned char> ram;
    size_t read = 0;
    try
    {
	read = m_dev.load(ram, filename, device_number, 1);
	printf("%d bytes\n",read);
    }
    catch (raspbiec_error &e)
    {
	printf("%s\n",e.what());
	read_error_channel(device_number);
	return;
    }

    if (is_directory)
    {
	basic_listing(ram);
    }
    else
    {
	write_local_file(ram, filename);
    }
}

void computer::save(const char *filename, int device_number)
{
    std::vector<unsigned char> prg;
    size_t prgsize = read_local_file(prg, filename);
    try
    {
	size_t written = m_dev.save(prg, filename, device_number, 0);
	printf("%d bytes\n",written);
    }
    catch (raspbiec_error &e)
    {
	printf("%s\n",e.what());
	read_error_channel(device_number);
    }
}

void computer::command(const char *command, int device_number)
{
    std::string asccmd(command);
    std::vector<unsigned char> cmd;
    ascii2petscii( asccmd, cmd );
    m_dev.send_data( cmd, device_number, 15 );
    read_error_channel(device_number);
}

void computer::read_error_channel(int device_number)
{
    std::vector<unsigned char> msg;
    m_dev.receive_data(msg, device_number, 15);

    std::string ascmsg;
    petscii2ascii( msg, ascmsg );
    printf("%s\n", ascmsg.c_str());
}

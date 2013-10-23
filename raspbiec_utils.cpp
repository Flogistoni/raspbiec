/*
 * Raspbiec - Commodore 64 & 1541 serial bus handler for Raspberry Pi
 * Copyright (C) 2013 Antti Paarlahti <antti.paarlahti@ovi.com>
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

#include "raspbiec_utils.h"
#include "raspbiec_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <dirent.h>

// PETSCII -> ASCII printable chars
// TODO: Find more substitutions
static unsigned char petscii[256] =
{   /*        _0  _1  _2  _3  _4  _5  _6  _7  _8  _9  _A  _B  _C  _D  _E  _F */
    /*00-0F*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ','\r',' ',' ',
    /*10-1F*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    /*20-2F*/ ' ','!','"','#','$','%','&','\'','(',')','*','+',',','-','.','/',
    /*30-3F*/ '0','1','2','3','4','5','6','7','8','9',':',';','<','=','>','?',
    /*40-4F*/ '@','a','b','c','d','e','f','g','h','i','j','k','l','m','n','o',
    /*50-5F*/ 'p','q','r','s','t','u','v','w','x','y','z','[',' ',']',' ',' ',
    /*60-6F*/ ' ','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
    /*70-7F*/ 'P','Q','R','S','T','U','V','W','X','Y','Z',' ',' ',' ',' ',' ',
    /*80-8F*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ','\n',' ',' ',
    /*90-9F*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    /*A0-AF*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    /*B0-BF*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    /*C0-CF*/ ' ','A','B','C','D','E','F','G','H','I','J','K','L','M','N','O',
    /*D0-DF*/ 'P','Q','R','S','T','U','V','W','X','Y','Z',' ',' ',' ',' ',' ',
    /*E0-EF*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
    /*F0-FF*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
};

const char petscii2ascii(const unsigned char petschar)
{
    if (petschar >= 0 && petschar <= 255)
    {
	return petscii[petschar];
    }
    else
    {
	return ' ';
    }
}

void petscii2ascii(const std::vector<unsigned char>& petschar,
		   std::string& ascchar)
{
    ascchar.clear();
    for (int i=0; i<petschar.size(); ++i)
    {
	ascchar.push_back(petscii2ascii(petschar[i]));
    }
}

const unsigned char ascii2petscii(const char ascchar)
{
    // Special treatment for space as it is the
    // default characted in the table
    if (ascchar == ' ')
    {
	return 0x20;
    }

    for (int i=0; i<=255; ++i)
    {
	if (ascchar == petscii[i])
	{
	    return i;
	}
    }

    return 0x20;
}

void ascii2petscii( const std::string& ascchar,
		    std::vector<unsigned char>& petschar )
{
    petschar.clear();
    for (int i=0; i<ascchar.size(); ++i)
    {
	petschar.push_back(ascii2petscii(ascchar[i]));
    }
}

void basic_listing(const std::vector<unsigned char>& prg)
{
    long nextline = 0;
    int linenum = 0;

    // i=2: skip load address
    for (long i=2, c=0; i<prg.size(); ++i,++c)
    {
	switch(c)
	{
	case 0:
	    nextline |= prg[i];
	    break;
	case 1:
	    nextline |= prg[i]<<8;
	    break;
	case 2:
	    linenum |= prg[i];
	    break;
	case 3:
	    linenum |= prg[i]<<8;
	    printf("%d ",linenum);
	    break;
	default:
	    if (prg[i] != 0)
	    {
		printf("%c", petscii2ascii(prg[i]));
		// TODO: Expand BASIC tokens
		// Now only handles directory listing
	    }
	    else // End of line
	    {
		c=-1;
		nextline = 0;
		linenum = 0;
		printf("\n");
	    }
	    break;
	}
    }
    printf("\n");
}


// RAII helper for file & dir pointers
// until unique_ptr is widely available
template <typename T, typename F>
class fdptr
{
    T *m_fdp;
    F m_del;
public:
    fdptr(T *fdp, F deleter) : m_fdp(fdp), m_del(deleter) {}
    ~fdptr() { F(m_del); }
    operator T*() { return m_fdp; }
};

size_t read_local_file(
    std::vector<unsigned char> &prg,
    const char *prgname)
{
    size_t read = 0;

    fdptr<FILE,int(*)(FILE*)>
	fp( fopen(prgname, "r"), fclose );
    if (!fp)
    {
	fprintf(stderr,"Could not open local file '%s'\n",prgname);
	throw raspbiec_error(IEC_FILE_NOT_FOUND);
    }

    struct stat sb;
    if (fstatat(AT_FDCWD, prgname, &sb, AT_NO_AUTOMOUNT) == -1)
    {
	fprintf(stderr,"Could not get size of file '%s'\n",prgname);
	throw raspbiec_error(IEC_FILE_NOT_FOUND);
    }

    prg.resize(sb.st_size);

    read = fread(prg.data(), sizeof(*prg.data()), prg.size(), fp);

    return read;
}

void write_local_file(
    const std::vector<unsigned char> &prg,
    const char *prgname)
{
    fdptr<FILE,int(*)(FILE*)>
	fp( fopen(prgname, "w"), fclose );
    if (fp)
    {
	size_t written = fwrite(prg.data(),
				sizeof(*prg.data()),
				prg.size(),
				fp);
	if (written != prg.size())
	{
	    fprintf(stderr, "Could not store the whole program "
		    "(%d bytes of %d)\n", written, prg.size());
	    throw raspbiec_error(IEC_GENERAL_ERROR);
	}
    }
}

bool local_file_exists(const char *prgname)
{
    fdptr<FILE,int(*)(FILE*)>
	fp( fopen(prgname, "r"), fclose );
    if (fp)
    {
	return true;
    }
    return false;
}


static const int16_t header_line[] =
{ 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0x12, 0x22,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x22, 0x20, 0x30, 0x30, 0x20, 0x32, 0x41, 0x00, -1 };
static const int16_t file_line[] =
{ 0x01, 0x01,   -2,   -3, 0x20, 0x22,   -4,   -4,
    -4,   -4,   -4,   -4,   -4,   -4,   -4,   -4,
    -4,   -4,   -4,   -4,   -4,   -4,   -5, 0x20,
  0x50, 0x52, 0x47, 0x20, 0x20, 0x20, 0x20, 0x00, -1 };
static const int16_t footer_line[] =
{ 0x01, 0x01,   -2,   -3, 0x42, 0x4C, 0x4F, 0x43,
  0x4B, 0x53, 0x20, 0x46, 0x52, 0x45, 0x45, 0x2E,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
  0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, -1 };

void read_local_dir(
    std::vector<unsigned char> &buf,
    const char *dirname)
{
    const int16_t *p;

    fdptr<DIR,int(*)(DIR*)>
	dirp( opendir(dirname), closedir );
    if (!dirp)
    {
	throw raspbiec_error(IEC_FILE_NOT_FOUND);
    }

    // Construct a BASIC listing from the directory info
    for (p = header_line; *p != -1; ++p)
    {
	buf.push_back(*p);
	printf("%c", petscii2ascii(*p));
    }
    printf("\n");

    struct stat sb;
    struct dirent *dp;
    while ((dp = readdir(dirp)) != NULL)
    {
	if (fstatat(dirfd(dirp),dp->d_name,&sb,AT_NO_AUTOMOUNT) == -1)
	    continue;

	int blocks = (sb.st_size + 253) / 254;
	if (blocks > 65535)
	    blocks = 65535;

	int i=0;
	for (p = file_line; *p != -1; ++p)
	{
	    switch(*p)
	    {
	    default:
		buf.push_back(*p);
		printf("%c", petscii2ascii(*p));
		break;
	    case -2: // Blocks LSB
		buf.push_back(blocks % 256);
		break;
	    case -3: // Blocks MSB
		buf.push_back(blocks / 256);
		printf("%d",blocks);
		if (blocks < 100)
		{
		    buf.push_back(0x20);
		    printf(" ");
		}
		if (blocks < 10)
		{
		    buf.push_back(0x20);
		    printf(" ");
		}
		break;
	    case -5: // Filename length hard limit
		if (i>=0) i=-1;
	    case -4: // State machine for name writing
		if (i>=0)
		{
		    char c = dp->d_name[i++];
		    if (c=='\0') i=-1;
		    else
		    {
			buf.push_back(ascii2petscii(c));
			printf("%c",c);
		    }
		}
		if (i==-1)
		{
		    buf.push_back(0x22);
		    printf("\"");
		    i=-2;
		}
		else if (i==-2)
		{
		    buf.push_back(0x20);
		    printf(" ");
		}
		break;
	    }
	}
	printf("\n");
    }

    unsigned long freeblocks = 0;
    struct statvfs sfb;
    if (statvfs(dirname, &sfb)==0)
    {
	freeblocks = sfb.f_bavail * (sfb.f_bsize/256);
	if (freeblocks > 65535)
	    freeblocks = 65535;
    }

    for (p = footer_line; *p != -1; ++p)
    {
	switch(*p)
	{
	default:
	    buf.push_back(*p);
	    printf("%c", petscii2ascii(*p));
	    break;
	case -2: // Freeblocks LSB
	    buf.push_back(freeblocks % 256);
	    break;
	case -3: // Freeblocks MSB
	    buf.push_back(freeblocks / 256);
	    printf("%d",freeblocks);
	    if (freeblocks < 100)
	    {
		buf.push_back(0x20);
		printf(" ");
	    }
	    if (freeblocks < 10)
	    {
		buf.push_back(0x20);
		printf(" ");
	    }
	    break;
	}
    }    
    printf("\n");
}

raspbiec_error::raspbiec_error(const int iec_status)
{
    m_status = iec_status;
}

raspbiec_error::~raspbiec_error() throw()
{
}

int raspbiec_error::status() const
{
    return m_status;
}

const char *raspbiec_error::what() const throw()
{
    switch(m_status)
    {
    case IEC_OK:
	return "OK";
    case IEC_ILLEGAL_DEVICE_NUMBER:
	return "illegal device number";
    case IEC_MISSING_FILENAME:
	return "missing filename";
    case IEC_FILE_NOT_FOUND:
	return "file not found";
    case IEC_WRITE_TIMEOUT:
	return "write timeout";
    case IEC_READ_TIMEOUT:
	return "read timeout";
    case IEC_DEVICE_NOT_PRESENT:
	return "device not present";
    case IEC_ILLEGAL_STATE:
	return "illegal state";
    case IEC_GENERAL_ERROR:
	return "general error";
    case IEC_PREV_BYTE_HAS_ERROR:
	return "previous byte has error";
    case IEC_FILE_EXISTS:
	return "file exists";
    case IEC_DRIVER_NOT_PRESENT:
	return "driver not present";
    case IEC_OUT_OF_MEMORY:
	return "out of memory";
    case IEC_UNKNOWN_MODE:
	return "unknown mode";
    case IEC_SIGNAL:
	return "caught a signal";
    case IEC_BUS_NOT_IDLE:
	return "IEC bus is not in idle state";
    case IEC_SAVE_ERROR:
	return "save error";
    default:
	snprintf(msg, sizeof msg, "raspbiec error %d (%c0x%X)\n",
		 m_status, m_status<0?'-':' ', m_status<0?-m_status:m_status);
	msg[(sizeof msg)-1] = '\0';
	return msg;
    }
}

struct sigaction raspbiec_sighandler::sa;
bool raspbiec_sighandler::sigactive;

void raspbiec_sighandler::setup(void)
{
    sigactive = false;
    sa.sa_flags = 0;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = sighandler;
    if (sigaction(SIGINT, &sa, NULL) == -1)
    {
	throw raspbiec_error(IEC_GENERAL_ERROR);
    }
}

void raspbiec_sighandler::react(bool want_to_catch)
{
    sigactive = want_to_catch;
}

void raspbiec_sighandler::sighandler(int sig)
{
    if (sigactive)
    {
	if (signal(sig, SIG_DFL) != SIG_ERR)
	{
	    raise(sig);
	}
	else
	{
	    abort();
	}
    }
}

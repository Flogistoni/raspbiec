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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <iterator>
#include "raspbiec_utils.h"
#include "raspbiec_common.h"
#include "raspbiec_diskimage.h"
#include "raspbiec_exception.h"

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

bool ispetsciinum(const unsigned char c)
{
	return c >= 0x30 && c <= 0x39;
}

bool ispetsciialpha(const unsigned char c)
{
	return
		(c >= 0x41 && c <= 0x5A) ||
		(c >= 0x61 && c <= 0x7A) ||
		(c >= 0xC1 && c <= 0xDA));
}

bool ispetsciialnum(const unsigned char c)
{
	return ispetsciinum(c) || ispetsciialpha(c);
}

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
    for (unsigned int i=0; i<petschar.size(); ++i)
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
    for (unsigned int i=0; i<ascchar.size(); ++i)
    {
		petschar.push_back(ascii2petscii(ascchar[i]));
    }
}

std::vector<unsigned char>::iterator petsciialnum(
	std::vector<unsigned char>::iterator petiter,
	const std::vector<unsigned char>::iterator petend,
	std::vector<unsigned char>& petstr)
{
	for (; petiter != petend && ispetsciialnum(*petiter); ++petiter)
	{
		petstr.push_back(*petiter);
	}
	return petiter;
}

void basic_listing(const databuf_t& prg)
{
    long nextline = 0;
    int linenum = 0;

    // i=2: skip load address
    for (unsigned long i=2, c=0; i<prg.size(); ++i,++c)
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
    ~fdptr() { if (m_fdp) (*m_del)(m_fdp); }
    operator T*() { return m_fdp; }
};

size_t read_local_file(databuf_t &data, const char *name)
{
    size_t read = 0;

    fdptr<FILE,int(*)(FILE*)>
	fp( fopen(name, "r"), fclose );
    if (!fp)
    {
	fprintf(stderr,"Could not open local file '%s'\n",name);
	throw raspbiec_error(IEC_FILE_NOT_FOUND);
    }

    struct stat sb;
    if (fstatat(AT_FDCWD, name, &sb, AT_NO_AUTOMOUNT) == -1)
    {
	fprintf(stderr,"Could not get size of file '%s'\n",name);
	throw raspbiec_error(IEC_FILE_NOT_FOUND);
    }

    data.resize(sb.st_size);

    read = fread(data.data(), sizeof(*data.data()), data.size(), fp);

    return read;
}

void write_local_file(const databuf_t &data, const char *name)
{
    fdptr<FILE,int(*)(FILE*)>
	fp( fopen(name, "w"), fclose );
    if (fp)
    {
	size_t written = fwrite(data.data(),
				sizeof(*data.data()),
				data.size(),
				fp);
	if (written != data.size())
	{
	    fprintf(stderr, "Could not store the whole data "
		    "(%ld bytes of %ld)\n", written, data.size());
	    throw raspbiec_error(IEC_GENERAL_ERROR);
	}
    }
}

int open_local_file(const char *name, const char* mode)
{
	if (!name || !mode)
		return -1;

	int oflag = 0;


	for (const char *p = mode; *p != '\0'; ++p)
	{
		switch(*p)
		{
		case 'r':
			oflag = O_RDONLY;
			break;
		case 'w':
			oflag = O_WRONLY | O_CREAT;
			break;
		case 'a':
			oflag = O_RDWR | O_APPEND;
			break;
		case '+':
			if (oflag & O_RDONLY)
			{
				oflag &= O_RDONLY;
				oflag |= O_RDWR;
			}
			if (oflag & O_WRONLY)
			{
				oflag &= O_WRONLY;
				oflag |= O_RDWR;
			}
			break;
		}
	}

	int fd = open(name, oflag);
	if (fd < 0)
	{
		fprintf(stderr,"Could not open local file '%s', error %d\n",name, fd);
		throw raspbiec_error(IEC_FILE_NOT_FOUND);
	}

#if 0
	if (fstatat(AT_FDCWD, ch.ascii, &ch.sb, AT_NO_AUTOMOUNT) == -1)
	{
		close_local_file(ch);
		fprintf(stderr,"Could not get stat of file '%s'\n",name);
		throw raspbiec_error(IEC_FILE_READ_ERROR);
	}
#endif
    return 0;
}

void close_local_file(int& handle)
{
	if (handle >= 0)
	{
		close(handle);
		handle = -1;
	}
}


bool local_file_exists(const char *name)
{
    fdptr<FILE,int(*)(FILE*)>
	fp( fopen(name, "r"), fclose );
    if (fp)
    {
	return true;
    }
    return false;
}

size_t read_from_local_file(const int handle, std::vector<unsigned char> &data, size_t amount)
{
	ssize_t rd = 0;
	if (handle >= 0)
	{
		data.resize(amount);
		rd = read(handle, data.data(), amount);
		if (read < 0)
		{
			fprintf(stderr, "Read error, errno %d", errno);
			throw raspbiec_error(IEC_FILE_READ_ERROR);
		}

		data.resize(rd);
	}
	return rd;
}

const_databuf_iter write_to_local_file(const int handle, const_databuf_iter begin, const_databuf_iter end)
{
	ssize_t written = 0;
	const_databuf_iter position = begin;
	if (handle >= 0)
	{
		written = write(handle, &(*begin), &(*end) - &(*begin));
		if (written < 0)
		{
			fprintf(stderr, "Write error, errno %d", errno);
			throw raspbiec_error(IEC_FILE_WRITE_ERROR);
		}
		std::advance(position, written);
	}
	return position;
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

void read_local_dir(databuf_t &buf, const char *dirname, bool verbose)
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
        if (verbose) printf("%c", petscii2ascii(*p));
    }
    if (verbose) printf("\n");

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
                if (verbose) printf("%c", petscii2ascii(*p));
                break;
            case -2: // Blocks LSB
                buf.push_back(blocks % 256);
                break;
            case -3: // Blocks MSB
                buf.push_back(blocks / 256);
                if (verbose) printf("%d",blocks);
                if (blocks < 100)
                {
                    buf.push_back(0x20);
                    if (verbose) printf(" ");
                }
                if (blocks < 10)
                {
                    buf.push_back(0x20);
                    if (verbose) printf(" ");
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
                        if (verbose) printf("%c",c);
                    }
                }
                if (i==-1)
                {
                    buf.push_back(0x22);
                    if (verbose) printf("\"");
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
        if (verbose) printf("\n");
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
            if (verbose) printf("%c", petscii2ascii(*p));
            break;
        case -2: // Freeblocks LSB
            buf.push_back(freeblocks % 256);
            break;
        case -3: // Freeblocks MSB
            buf.push_back(freeblocks / 256);
            if (verbose) printf("%lu ",freeblocks);
            if (freeblocks < 100)
            {
                buf.push_back(0x20);
                if (verbose) printf(" ");
            }
            if (freeblocks < 10)
            {
                buf.push_back(0x20);
                if (verbose) printf(" ");
            }
            break;
        }
    }
    if (verbose) printf("\n");
}

static const int16_t header_line_diskimage[] =
{ 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0x12, 0x22,
    -4,   -4,   -4,   -4,   -4,   -4,   -4,   -4,
    -4,   -4,   -4,   -4,   -4,   -4,   -4,   -4,
    -5,   -4,   -4,   -4,   -4,   -4,   -4, 0x00, -1 };

void read_diskimage_dir(
    std::vector<unsigned char> &buf,
    Diskimage& diskimage,
    bool verbose)

{
    const int16_t *p;

    Diskimage::Dirstate dirstate;
    Diskimage::Direntry direntry;

    if (!diskimage.opendir(dirstate))
    {
        throw raspbiec_error(IEC_DISK_IMAGE_ERROR);
    }

    // Construct a BASIC listing from the directory info

    int i=0;
    for (p = header_line_diskimage; *p != -1; ++p)
    {
        switch(*p)
        {
        default:
            buf.push_back(*p);
            if (verbose) printf("%c", petscii2ascii(*p));
            break;

        case -4: // State machine for name writing
        {
            char c = dirstate.name_id[i++];
            if (c==0xA0) c=0x20;
            buf.push_back(c);
            if (verbose) printf("%c",petscii2ascii(c));
            break;
        }

        case -5: // Diskname end quote
            ++i;
            buf.push_back(0x22);
            if (verbose) printf("%c",petscii2ascii(0x22));
            break;
        }
    }
    if (verbose) printf("\n");

    while (diskimage.readdir(dirstate,direntry))
    {
        if (direntry.filetype == 0x00)
            continue; // TODO: filetypes

        int blocks = direntry.size_hi * 0x100 + direntry.size_lo;

        i=0;
        for (p = file_line; *p != -1; ++p)
        {
            switch(*p)
            {
            default:
                buf.push_back(*p);
                if (verbose) printf("%c", petscii2ascii(*p));
                break;
            case -2: // Blocks LSB
                buf.push_back(direntry.size_lo);
                break;
            case -3: // Blocks MSB
                buf.push_back(direntry.size_hi);
                if (verbose) printf("%d",blocks);
                if (blocks < 100)
                {
                    buf.push_back(0x20);
                    if (verbose) printf(" ");
                }
                if (blocks < 10)
                {
                    buf.push_back(0x20);
                    if (verbose) printf(" ");
                }
                break;
            case -5: // Filename length hard limit
                if (i>=0) i=-1;
            case -4: // State machine for name writing
                if (i>=0)
                {
                    char c = direntry.name[i++];
                    if (c==0xA0) i=-1;
                    else
                    {
                        buf.push_back(c);
                        if (verbose) printf("%c",petscii2ascii(c));
                    }
                }
                if (i==-1)
                {
                    buf.push_back(0x22);
                    if (verbose) printf("\"");
                    i=-2;
                }
                else if (i==-2)
                {
                    buf.push_back(0x20);
                    if (verbose) printf(" ");
                }
                break;
            }
        }
        if (verbose) printf("\n");
    }

    for (p = footer_line; *p != -1; ++p)
    {
        switch(*p)
        {
        default:
            buf.push_back(*p);
            if (verbose) printf("%c", petscii2ascii(*p));
            break;
        case -2: // Freeblocks LSB
            buf.push_back(dirstate.free_lo);
            break;
        case -3: // Freeblocks MSB
            buf.push_back(dirstate.free_hi);
            int freeblocks = dirstate.free_hi * 0x100 + dirstate.free_lo;
            if (verbose) printf("%d ",freeblocks);
            if (freeblocks < 100)
            {
                buf.push_back(0x20);
                if (verbose) printf(" ");
            }
            if (freeblocks < 10)
            {
                buf.push_back(0x20);
                if (verbose) printf(" ");
            }
            break;
        }
    }
    if (verbose) printf("\n");
}

/*********************************************************************/

static const char* raspbiecdevname = "/dev/raspbiec";


pipefd::pipefd() :
		m_fd_size(1)
{
	for (int i=0; i<4; ++i) m_fd[i] = -1;
}

pipefd::~pipefd()
{
	close_pipe();
}

void pipefd::move(pipefd &other)
{
	close_pipe();
	for (int i=0; i<4; ++i) m_fd[i] = other.m_fd[i];
	m_fd_size = other.m_fd_size;
	for (int i=0; i<4; ++i) other.m_fd[i] = -1;
	other.m_fd_size = 1;
}

void pipefd::close_pipe()
{
	for (int i=0; i<m_fd_size; ++i)
	{
		if (m_fd[i] >= 0) ::close(m_fd[i]);
	}
	for (int i=0; i<2; ++i)
	{
		m_fd[i] = -1;
	}
	m_fd_size = 1;
}

void pipefd::open_pipe()
{
	close_pipe();
    m_fd_size = 4;
    if (pipe(&m_fd[0]) == -1 ||
    	pipe(&m_fd[2]) == -1)
    {
    	close_pipe();
		throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);
    }
}

void pipefd::open_dev()
{
	close_pipe();
	int fd_dev = open(raspbiecdevname, O_RDWR);
	if (fd_dev < 0)
	{
		int deverr = errno;
		fprintf(stderr,"Cannot open %s\n",raspbiecdevname);
		if (deverr == EREMOTEIO)
			throw raspbiec_error(IEC_BUS_NOT_IDLE);
		else
			throw raspbiec_error(IEC_DRIVER_NOT_PRESENT);
	}
	m_fd[0] = fd_dev;
}

bool pipefd::is_open_directional()
{
	if (is_device()) // bidirectional
	{
		return (m_fd[0] >= 0);
	}
	// two unidirectional pipes, 0 and 3 or 1 and 2
	if (m_fd[0] >= 0 && m_fd[1] < 0 && m_fd[2] < 0 && m_fd[3] >= 0) return true;
	if (m_fd[0] < 0 && m_fd[1] >= 0 && m_fd[2] >= 0 && m_fd[3] < 0) return true;
	return false;
}

bool pipefd::is_open_nondirectional()
{
	for (int i=0; i<m_fd_size; ++i) if (m_fd[i] < 0) return false;
	return true;
}

bool pipefd::is_device()
{
    return 1 == m_fd_size;
}

void pipefd::set_write(int *fd)
{
	if (fd[0] >= 0)
	{
		close(fd[0]); // Close unused read end
		fd[0] = -1;
	}
	if (fd[1] < 0) // Check write end is open
	{
		throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);
	}
}

void pipefd::set_read(int *fd)
{
	if (fd[1] >= 0)
	{
		close(fd[1]); // Close unused write end
		fd[1] = -1;
	}
	if (fd[0] < 0) // Check read end is open
	{
		throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);
	}
}

void pipefd::set_direction(bool fwd)
{
	if (!is_device()) // two unidirectional pipes
	{
		if (fwd) // these directions are arbitrary
		{
			set_write(&m_fd[0]);
			set_read(&m_fd[2]);
		}
		else
		{
			set_read(&m_fd[0]);
			set_write(&m_fd[2]);
		}
	}
}

int pipefd::write_end()
{
	if (!is_open_directional()) throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);

	if (is_device()) // bidirectional
	{
		return m_fd[0];
	}
	// two unidirectional pipes
	return (m_fd[1] >= 0) ? m_fd[1] : m_fd[3];
}

int pipefd::read_end()
{
	if (!is_open_directional()) throw raspbiec_error(IEC_DEVICE_NOT_PRESENT);

	if (is_device()) // bidirectional
	{
		return m_fd[0];
	}
	// two unidirectional pipes
	return (m_fd[0] >= 0) ? m_fd[0] : m_fd[2];
}

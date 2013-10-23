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

#ifndef RASPBIEC_UTILS_H
#define RASPBIEC_UTILS_H

#include <exception>
#include <vector>
#include <string>

const char petscii2ascii(const unsigned char petschar);
void petscii2ascii(const std::vector<unsigned char>& petschar,
		   std::string& ascchar);

const unsigned char ascii2petscii(const char ascchar);
void ascii2petscii( const std::string& ascchar,
		    std::vector<unsigned char>& petschar );

void basic_listing(const std::vector<unsigned char>& prg);

size_t read_local_file(
    std::vector<unsigned char> &prg,
    const char *prgname);

void write_local_file(
    const std::vector<unsigned char> &prg,
    const char *prgname);

bool local_file_exists(const char *prgname);

void read_local_dir(
    std::vector<unsigned char> &buf,
    const char *dirname);

class raspbiec_error : public std::exception
{
public:
    explicit raspbiec_error(const int iec_status);
    virtual ~raspbiec_error() throw();
    virtual const char* what() const throw();
    int status() const;
	
private:
    int m_status;
    mutable char msg[30];
};


class raspbiec_sighandler
{
public:
    static void setup(void);
    static void react(bool want_to_catch);

private:	 
    static void sighandler(int);
	
private:
    static struct sigaction sa;
    static bool sigactive;
};

#endif // RASPBIEC_UTILS_H

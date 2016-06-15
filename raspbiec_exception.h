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

#ifndef RASPBIEC_EXCEPTION_H
#define RASPBIEC_EXCEPTION_H

#include <exception>

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

#endif // RASPBIEC_EXCEPTION_H

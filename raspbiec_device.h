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

#ifndef RASPBIEC_DEVICE_H
#define RASPBIEC_DEVICE_H

#include <cstddef>
#include <stdint.h>
#include <vector>

class device
{
public:
    device();
    ~device();

    enum Identity
    {
	computer = -1,
	drive_8  =  8,
	drive_9  =  9,
	drive_10 = 10,
	drive_11 = 11
    };

    void set_identity(const int identity);

    // When functioning as a drive
    enum Command
    {
	Unknown,
	Open,
	Close,
	Save,
	Load,
	Unlisten,
	Untalk,
	Exit,
	OpenOtherDevice,
	CloseOtherDevice,
	SaveOtherDevice,
	LoadOtherDevice
    };

    Command receive_command(int device_number, int &chan);

    // When functioning as a computer
    size_t load( std::vector<unsigned char>& load_buf,
		 const char *const name,
		 int device_number,
		 int secondary_address );
    
    size_t save( const std::vector<unsigned char>& save_buf, 
		 const char *const name,
		 int device_number,
		 int secondary_address );

    void open_file( const char *name, int device, int secondary_address );
    void close_file(int device, int secondary_address);

    size_t send_data(const std::vector<unsigned char>& data_buf,
		     int device_number,
		     int channel );
    size_t receive_data(std::vector<unsigned char>& data_buf,
			int device_number,
			int channel );

    enum {
	timeout_default = -1,
	timeout_infinite = 0
    };

    // Common routines
    size_t send_to_bus(const std::vector<unsigned char>& data_buf);
    size_t receive_from_bus(std::vector<unsigned char>& data_buf,
			    long timeout_ms = timeout_default);
    size_t send_to_bus_verbose(const std::vector<unsigned char>& data_buf);
    size_t receive_from_bus_verbose(std::vector<unsigned char>& data_buf,
				    long timeout_ms = timeout_default);
    void talk( int device );
    void listen( int device );
    void untalk();
    void unlisten();
    void open_cmd(int secondary_address);
    void close_cmd(int secondary_address);
    void data_listen(int secondary_address);
    void data_talk(int secondary_address);
    void command( int command );
    void secondary_command(int secondary_command, bool talk);
    void send_byte_buffered_init(void);
    void send_byte_buffered( int16_t byte );
    void send_last_byte();
    void send_byte( int16_t byte );
    int16_t receive_byte( long timeout_ms = timeout_default );
    void clear_error(void);

private:
    int devfd;
    bool buffered;
    int16_t buffered_byte;
    int16_t lasterror;
    bool verbose;
};

#endif // RASPBIEC_DEVICE_H

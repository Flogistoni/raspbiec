/*
 * raspbiec_types.h
 *
 *  Created on: 10 May 2015
 *      Author: ap
 */

#ifndef RASPBIEC_TYPES_H
#define RASPBIEC_TYPES_H

#include <vector>

typedef std::vector<unsigned char> databuf_t;
typedef databuf_t::iterator databuf_iter;
typedef databuf_t::const_iterator const_databuf_iter;
typedef std::back_insert_iterator<databuf_t> databuf_back_insert;

#endif /* RASPBIEC_TYPES_H */

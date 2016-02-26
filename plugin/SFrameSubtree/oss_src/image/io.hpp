/**
 * Copyright (C) 2015 Dato, Inc.
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license. See the LICENSE file for details.
 */
#ifndef GRAPHLAB_IMAGE_IO_IMPL_HPP
#define GRAPHLAB_IMAGE_IO_IMPL_HPP

#include <string>
#include <image/image_type.hpp>

namespace graphlab {
/**
 * Write the pixels into file as jpeg
 * Only works with local files.
 */
void write_image(const std::string& filename, char* data, size_t width, size_t height, size_t channels, Format format);

/**
 * Read the content from url and return an image type object.
 * Throws an exception if failing to decode as format_hint.
 */
image_type read_image(const std::string& url, const std::string& format_hint);

/**
 * Read raw image data from URL, store data in data and length. Store image information such as width, height, channels, and format.
 */
void read_raw_image(const std::string& url, char** data, size_t& length, size_t& width, size_t& height, size_t& channels, Format& format, const std::string& format_hint);

/**
 * Parse the image information, set width, height and channels using libjpeg.
 */
void parse_jpeg(const char* data, size_t length,
                size_t& width, size_t& height, size_t& channels);

void decode_jpeg(const char* data, size_t length, char** decoded_data, size_t& out_length);

/**
 * Parse the image information, set width, height and channels using libpng.
 */
void parse_png(const char* data, size_t length,
               size_t& width, size_t& height, size_t& channels);

void decode_png(const char* data, size_t length, char** decoded_data, size_t& out_length);

void encode_png(const char* data, size_t width, size_t height, size_t channels, char** out_data, size_t& out_length); 
/**************************************************************************/
/*                                                                        */
/*                             Prototype Code                             */
/*                                                                        */
/**************************************************************************/
/**
 * Parse the image information, set width, height and channels using boost_gil.
 */
void boost_parse_image(std::string filename, size_t& width, size_t& height, size_t& channels, Format& format, size_t& image_data_size, std::string format_string);

/**
 * Read the content of jpeg image and return the pixel array into out_data
 * Only works with local files.
 */
void boost_read_image(std::string filename, char** out_data, size_t& width, size_t& height, size_t& channels, Format& format, size_t& image_data_size, std::string format_string);

}
#endif

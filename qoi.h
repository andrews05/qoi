/*

Copyright (c) 2021, Dominic Szablewski - https://phoboslab.org (original QOI format)
SPDX-License-Identifier: MIT


QOI2 - Lossless image format inspired by QOI “Quite OK Image” format

Incompatible adaptation of QOI format


-- Synopsis

// Define `QOI_IMPLEMENTATION` in *one* C/C++ file before including this
// library to create the implementation.

#define QOI_IMPLEMENTATION
#include "qoi.h"

// Encode and store an RGBA buffer to the file system. The qoi_desc describes
// the input pixel data.
qoi_write("image_new.qoi", rgba_pixels, &(qoi_desc){
	.width = 1920,
	.height = 1080,
	.channels = 4,
	.colorspace = QOI_SRGB
});

// Load and decode a QOI image from the file system into a 32bbp RGBA buffer.
// The qoi_desc struct will be filled with the width, height, number of channels
// and colorspace read from the file header.
qoi_desc desc;
void *rgba_pixels = qoi_read("image.qoi", &desc, 4);



-- Documentation

This library provides the following functions;
- qoi_read    -- read and decode a QOI file
- qoi_decode  -- decode the raw bytes of a QOI image from memory
- qoi_write   -- encode and write a QOI file
- qoi_encode  -- encode an rgba buffer into a QOI image in memory

See the function declaration below for the signature and more information.

If you don't want/need the qoi_read and qoi_write functions, you can define
QOI_NO_STDIO before including this library.

This library uses malloc() and free(). To supply your own malloc implementation
you can define QOI_MALLOC and QOI_FREE before including this library.

This library uses memset() to zero-initialize the index. To supply your own
implementation you can define QOI_ZEROARR before including this library.


-- Data Format

A QOI file has a 14 byte header, followed by any number of data "chunks" and an
8-byte end marker.

struct qoi_header_t {
	char     magic[4];   // magic bytes "qoi2"
	uint32_t width;      // image width in pixels (BE)
	uint32_t height;     // image height in pixels (BE)
	uint8_t  channels;   // 3 = RGB, 4 = RGBA
	uint8_t  colorspace; // 0 = sRGB with linear alpha, 1 = all channels linear
};

Images are encoded row by row, left to right, top to bottom. The decoder and
encoder start with {r: 0, g: 0, b: 0, a: 255} as the previous pixel value. An
image is complete when all pixels specified by width * height have been covered.

Pixels are encoded as
 - a run of the previous pixel
 - an index into an array of previously seen pixels
 - a difference to the previous pixel value in r,g,b
 - full r,g,b or a or gray values

The color channels are assumed to not be premultiplied with the alpha channel
("un-premultiplied alpha").

Each chunk starts with a tag, followed by a number of data bits. The bit length
of chunks is divisible by 8 - i.e. all chunks are byte aligned. All values
encoded in these data bits have the most significant bit on the left.

The byte stream's end is marked with 4 0xff bytes.

A running FIFO array[64] (zero-initialized) of pixel values is maintained by the
encoder and decoder. Every pixel en-/decoded by the QOI_OP_LUMA (and variants),
QOI_OP_GRAY and QOI_OP_RGB chunks is written to this array. The write position
starts at 0 and is incremented with each pixel written. The position wraps back
to 0 when it reaches 64. I.e:
	index[index_pos % 64] = current_pixel;
	index_pos = index_pos + 1;

An encoder can search this array for the current pixel value and, if a match is
found, emit a QOI_OP_INDEX with the position within the array.


The possible chunks are:


.- QOI_OP_INDEX ----------.
|         Byte[0]         |
|  7  6  5  4  3  2  1  0 |
|-------+-----------------|
|  1  0 |     index       |
`-------------------------`
2-bit tag b10
6-bit index into the color index array: 0..63


.- QOI_OP_LUMA -----------.
|         Byte[0]         |
|  7  6  5  4  3  2  1  0 |
|----+--------+-----+-----|
|  0 | g diff | drg | dbg |
`-------------------------`
1-bit tag b0
3-bit green channel difference from the previous pixel -4..3
2-bit   red channel difference minus green channel difference -1..2 or -2..1
2-bit  blue channel difference minus green channel difference -1..2 or -2..1

The green channel is used to indicate the general direction of change and is
encoded in 3 bits. The red and green channels (dr and db) base their diffs off
of the green channel difference and are encoded in 2 bits. I.e.:
	dr_dg = (last_px.r - cur_px.r) - (last_px.g - cur_px.g)
	db_dg = (last_px.b - cur_px.b) - (last_px.g - cur_px.g)

The difference to the current channel values are using a wraparound operation, 
so "1 - 2" will result in 255, while "255 + 1" will result in 0.

Values are stored as unsigned integers with a bias of 4 for the green channel
and a bias of 1 or 2 for the red and blue channel depending on the direction
(sign bit) of the green channel.


.- QOI_OP_LUMA2 ------------------------------------.
|         Byte[0]         |         Byte[1]         |
|  7  6  5  4  3  2  1  0 |  7  6  5  4  3  2  1  0 |
|----------+--------------+-------------+-----------|
|  1  1  0 |  green diff  |   dr - dg   |  db - dg  |
`---------------------------------------------------`
3-bit tag b110
5-bit green channel difference from the previous pixel -16..15
4-bit   red channel difference minus green channel difference -8..7
4-bit  blue channel difference minus green channel difference -8..7

The green channel is used to indicate the general direction of change and is 
encoded in 5 bits. The red and green channels (dr and db) base their diffs off
of the green channel difference and are encoded in 4 bits.

Values are stored as unsigned integers with a bias of 16 for the green channel
and a bias of 8 for the red and blue channel.

The alpha value remains unchanged from the previous pixel.


.- QOI_OP_LUMA3 ------------------------------------.-------------------------.
|         Byte[0]         |         Byte[1]         |         Byte[2]         |
|  7  6  5  4  3  2  1  0 |  7  6  5  4  3  2  1  0 |  7  6  5  4  3  2  1  0 |
|-------------+-----------+-------+-----------------+-------------------------|
|  1  1  1  0 |      dr - dg      |     db - dg     |        green diff       |
`-----------------------------------------------------------------------------`
4-bit tag b1110
6-bit   red channel difference minus green channel difference -32..31
6-bit  blue channel difference minus green channel difference -32..31
8-bit green channel difference from the previous pixel -128..127

The green channel is used to indicate the general direction of change and is
encoded in 8 bits. The red and green channels (dr and db) base their diffs off
of the green channel difference and are encoded in 6 bits.

Values are stored as unsigned integers with a bias of 128 for the green channel
and a bias of 32 for the red and blue channel.


.- QOI_OP_RUN ------------.
|         Byte[0]         |
|  7  6  5  4  3  2  1  0 |
|----------------+--------|
|  1  1  1  1  0 |  run   |
`-------------------------`
5-bit tag b11110
3-bit run-length repeating the previous pixel: 1..8

The run-length is stored with a bias of 1.


.- QOI_OP_RUN2 ---------------------.
|         Byte[0]         | Byte[1] |
|  7  6  5  4  3  2  1  0 | 7 .. 0  |
|-------------------+-----+---------|
|  1  1  1  1  1  0 |      run      |
`-----------------------------------`
6-bit tag b111110
10-bit run-length repeating the previous pixel: 1..1024

The run-length is stored with a bias of 1.


.- QOI_OP_GRAY ---------------------.
|         Byte[0]         | Byte[1] |
|  7  6  5  4  3  2  1  0 | 7 .. 0  |
|-------------------------+---------|
|  1  1  1  1  1  1  0  0 |  gray   |
`-----------------------------------`
8-bit tag b11111100
8-bit gray channel value


.- QOI_OP_RGB ------------------------------------------.
|         Byte[0]         | Byte[1] | Byte[2] | Byte[3] |
|  7  6  5  4  3  2  1  0 | 7 .. 0  | 7 .. 0  | 7 .. 0  |
|-------------------------+---------+---------+---------|
|  1  1  1  1  1  1  0  1 |   red   |  green  |  blue   |
`-------------------------------------------------------`
8-bit tag b11111101
8-bit   red channel value
8-bit green channel value
8-bit  blue channel value


.- QOI_OP_A ------------------------.
|         Byte[0]         | Byte[1] |
|  7  6  5  4  3  2  1  0 | 7 .. 0  |
|-------------------------+---------|
|  1  1  1  1  1  1  1  0 |  alpha  |
`-----------------------------------`
8-bit tag b11111110
8-bit alpha channel value


.- QOI_OP_END ------------.
|         Byte[0]         |
|  7  6  5  4  3  2  1  0 |
|-------------------------|
|  1  1  1  1  1  1  1  1 |
`-------------------------`
8-bit tag b11111111


The byte stream is padded at the end with four 0xff bytes. Since the longest
legal chunk is 4 bytes (QOI_OP_RGB), with this padding it is possible to check
for an overrun only once per decode loop iteration. These 0xff bytes also mark
the end of the data stream, as an encoder should never produce four consecutive
0xff bytes within the stream.

*/


/* -----------------------------------------------------------------------------
Header - Public functions */

#ifndef QOI_H
#define QOI_H

#ifdef __cplusplus
extern "C" {
#endif

/* A pointer to a qoi_desc struct has to be supplied to all of qoi's functions.
It describes either the input format (for qoi_write and qoi_encode), or is
filled with the description read from the file header (for qoi_read and
qoi_decode).

The colorspace in this qoi_desc is an enum where
	0 = sRGB, i.e. gamma scaled RGB channels and a linear alpha channel
	1 = all channels are linear
You may use the constants QOI_SRGB or QOI_LINEAR. The colorspace is purely
informative. It will be saved to the file header, but does not affect
how chunks are en-/decoded. */

#define QOI_SRGB   0
#define QOI_LINEAR 1

typedef struct {
	unsigned int width;
	unsigned int height;
	unsigned char channels;
	unsigned char colorspace;
} qoi_desc;

#ifndef QOI_NO_STDIO

/* Encode raw RGB or RGBA pixels into a QOI image and write it to the file
system. The qoi_desc struct must be filled with the image width, height,
number of channels (3 = RGB, 4 = RGBA) and the colorspace.

The function returns 0 on failure (invalid parameters, or fopen or malloc
failed) or the number of bytes written on success. */

int qoi_write(const char *filename, const void *data, const qoi_desc *desc);


/* Read and decode a QOI image from the file system. If channels is 0, the
number of channels from the file header is used. If channels is 3 or 4 the
output format will be forced into this number of channels.

The function either returns NULL on failure (invalid data, or malloc or fopen
failed) or a pointer to the decoded pixels. On success, the qoi_desc struct
will be filled with the description from the file header.

The returned pixel data should be free()d after use. */

void *qoi_read(const char *filename, qoi_desc *desc, int channels);

#endif /* QOI_NO_STDIO */


/* Encode raw RGB or RGBA pixels into a QOI image in memory.

The function either returns NULL on failure (invalid parameters or malloc
failed) or a pointer to the encoded data on success. On success the out_len
is set to the size in bytes of the encoded data.

The returned qoi data should be free()d after use. */

void *qoi_encode(const void *data, const qoi_desc *desc, int *out_len);


/* Decode a QOI image from memory.

The function either returns NULL on failure (invalid parameters or malloc
failed) or a pointer to the decoded pixels. On success, the qoi_desc struct
is filled with the description from the file header.

The returned pixel data should be free()d after use. */

void *qoi_decode(const void *data, int size, qoi_desc *desc, int channels);


#ifdef __cplusplus
}
#endif
#endif /* QOI_H */


/* -----------------------------------------------------------------------------
Implementation */

#ifdef QOI_IMPLEMENTATION
#include <stdlib.h>
#include <string.h>

#ifndef QOI_MALLOC
	#define QOI_MALLOC(sz) malloc(sz)
	#define QOI_FREE(p)    free(p)
#endif
#ifndef QOI_ZEROARR
	#define QOI_ZEROARR(a) memset((a),0,sizeof(a))
#endif

#define QOI_OP_LUMA   0x00 /* 0xxxxxxx */
#define QOI_OP_INDEX  0x80 /* 10xxxxxx */
#define QOI_OP_LUMA2  0xc0 /* 110xxxxx */
#define QOI_OP_LUMA3  0xe0 /* 1110xxxx */
#define QOI_OP_RUN    0xf0 /* 11110xxx */
#define QOI_OP_RUN2   0xf8 /* 111110xx */
#define QOI_OP_GRAY   0xfc /* 11111100 */
#define QOI_OP_RGB    0xfd /* 11111101 */
#define QOI_OP_A      0xfe /* 11111110 */
#define QOI_OP_END    0xff /* 11111111 */

#define QOI_MAGIC \
	(((unsigned int)'q') << 24 | ((unsigned int)'o') << 16 | \
	 ((unsigned int)'i') <<  8 | ((unsigned int)'2'))
#define QOI_HEADER_SIZE 14

/* To not have to linearly search through the color index array, we use a hash 
of the color value to quickly lookup the index position in a hash table. */
#define QOI_COLOR_HASH(C) (((C.v * 2654435769) >> 22) & 1023)

/* 2GB is the max file size that this implementation can safely handle. We guard
against anything larger than that, assuming the worst case with 6 bytes per
pixel, rounded down to a nice clean value. 350 million pixels ought to be
enough for anybody. */
#define QOI_PIXELS_MAX ((unsigned int)350000000)

typedef union {
	struct { unsigned char r, g, b, a; } rgba;
	unsigned int v;
} qoi_rgba_t;

static const unsigned char qoi_padding[4] = {0xff,0xff,0xff,0xff};

static void qoi_write_32(unsigned char *bytes, int *p, unsigned int v) {
	bytes[(*p)++] = (0xff000000 & v) >> 24;
	bytes[(*p)++] = (0x00ff0000 & v) >> 16;
	bytes[(*p)++] = (0x0000ff00 & v) >> 8;
	bytes[(*p)++] = (0x000000ff & v);
}

static unsigned int qoi_read_32(const unsigned char *bytes, int *p) {
	unsigned int a = bytes[(*p)++];
	unsigned int b = bytes[(*p)++];
	unsigned int c = bytes[(*p)++];
	unsigned int d = bytes[(*p)++];
	return a << 24 | b << 16 | c << 8 | d;
}

void *qoi_encode(const void *data, const qoi_desc *desc, int *out_len) {
	int i, max_size, p, run;
	int px_len, px_end, px_pos, channels;
	unsigned char *bytes;
	const unsigned char *pixels;
	unsigned char index_lookup[1024];
	unsigned int index_pos = 0;
	qoi_rgba_t index[64];
	qoi_rgba_t px, px_prev;

	if (
		data == NULL || out_len == NULL || desc == NULL ||
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 1 ||
		desc->height >= QOI_PIXELS_MAX / desc->width
	) {
		return NULL;
	}

	if (desc->channels == 3) {
		max_size = 
			desc->width * desc->height * 4 + 
			QOI_HEADER_SIZE + (int)sizeof(qoi_padding);
	}
	else {
		max_size = 
			desc->width * desc->height * 6 + 
			QOI_HEADER_SIZE + (int)sizeof(qoi_padding);
	}

	p = 0;
	bytes = (unsigned char *) QOI_MALLOC(max_size);
	if (!bytes) {
		return NULL;
	}

	qoi_write_32(bytes, &p, QOI_MAGIC);
	qoi_write_32(bytes, &p, desc->width);
	qoi_write_32(bytes, &p, desc->height);
	bytes[p++] = desc->channels;
	bytes[p++] = desc->colorspace;

	pixels = (const unsigned char *)data;

	QOI_ZEROARR(index);
	QOI_ZEROARR(index_lookup);

	run = 0;
	px_prev.rgba.r = 0;
	px_prev.rgba.g = 0;
	px_prev.rgba.b = 0;
	px_prev.rgba.a = 255;
	px = px_prev;

	px_len = desc->width * desc->height * desc->channels;
	px_end = px_len - desc->channels;
	channels = desc->channels;

	for (px_pos = 0; px_pos < px_len; px_pos += channels) {
		px.rgba.r = pixels[px_pos + 0];
		px.rgba.g = pixels[px_pos + 1];
		px.rgba.b = pixels[px_pos + 2];

		if (channels == 4) {
			px.rgba.a = pixels[px_pos + 3];
		}

		if (px.v == px_prev.v) {
			run++;
			if (run == 1024 || px_pos == px_end) {
				run--;
				bytes[p++] = QOI_OP_RUN2 | ((run >> 8) & 3);
				bytes[p++] = run & 0xff;
				run = 0;
			}
		}
		else {
			int hash = QOI_COLOR_HASH(px);

			if (run > 0) {
				run--;
				if (run < 8) {
					bytes[p++] = QOI_OP_RUN | run;
				}
				else {
					bytes[p++] = QOI_OP_RUN2 | ((run >> 8) & 3);
					bytes[p++] = run & 0xff;
				}
				run = 0;
			}

			if (index[index_lookup[hash]].v == px.v) {
				bytes[p++] = QOI_OP_INDEX | index_lookup[hash];
			}
			else {
				index_lookup[hash] = index_pos;
				index[index_pos] = px;
				index_pos = (index_pos + 1) & 63;

				if (px.rgba.a != px_prev.rgba.a) {
					bytes[p++] = QOI_OP_A;
					bytes[p++] = px.rgba.a;
				}

				signed char vg   = px.rgba.g - px_prev.rgba.g;
				signed char vg_r = px.rgba.r - px_prev.rgba.r - vg;
				signed char vg_b = px.rgba.b - px_prev.rgba.b - vg;

				if (
					vg   >= -4 && vg   <  0 &&
					vg_r >= -1 && vg_r <= 2 &&
					vg_b >= -1 && vg_b <= 2
				) {
					bytes[p++] = QOI_OP_LUMA | (vg + 4) << 4 | (vg_r + 1) << 2 | (vg_b + 1);
				}
				else if (
					vg   >=  0 && vg   <= 3 &&
					vg_r >= -2 && vg_r <= 1 &&
					vg_b >= -2 && vg_b <= 1
				) {
					bytes[p++] = QOI_OP_LUMA | (vg + 4) << 4 | (vg_r + 2) << 2 | (vg_b + 2);
				}
				else if (
					px.rgba.g == px.rgba.r &&
					px.rgba.g == px.rgba.b
				) {
					bytes[p++] = QOI_OP_GRAY;
					bytes[p++] = px.rgba.g;					
				}
				else if (
					vg_r >=  -8 && vg_r <=  7 &&
					vg   >= -16 && vg   <= 15 &&
					vg_b >=  -8 && vg_b <=  7
				) {
					bytes[p++] = QOI_OP_LUMA2    | (vg   + 16);
					bytes[p++] = (vg_r + 8) << 4 | (vg_b +  8);
				}
				else if (
					vg_r >= -32 && vg_r <= 31 &&
					vg_b >= -32 && vg_b <= 31
				) {
					bytes[p++] = QOI_OP_LUMA3           | ((vg_r + 32) >> 2);
					bytes[p++] = ((vg_r + 32) & 3) << 6 |  (vg_b + 32);
					bytes[p++] = vg + 128;
				}
				else {
					bytes[p++] = QOI_OP_RGB;
					bytes[p++] = px.rgba.r;
					bytes[p++] = px.rgba.g;
					bytes[p++] = px.rgba.b;
				}
			}
			px_prev = px;
		}
	}

	for (i = 0; i < (int)sizeof(qoi_padding); i++) {
		bytes[p++] = qoi_padding[i];
	}

	*out_len = p;
	return bytes;
}

void *qoi_decode(const void *data, int size, qoi_desc *desc, int channels) {
	const unsigned char *bytes;
	unsigned int header_magic;
	unsigned char *pixels;
	qoi_rgba_t index[64];
	qoi_rgba_t px;
	int px_len, chunks_len, px_pos;
	int p = 0, run = 0;
	int index_pos = 0;

	if (
		data == NULL || desc == NULL ||
		(channels != 0 && channels != 3 && channels != 4) ||
		size < QOI_HEADER_SIZE + (int)sizeof(qoi_padding)
	) {
		return NULL;
	}

	bytes = (const unsigned char *)data;

	header_magic = qoi_read_32(bytes, &p);
	desc->width = qoi_read_32(bytes, &p);
	desc->height = qoi_read_32(bytes, &p);
	desc->channels = bytes[p++];
	desc->colorspace = bytes[p++];

	if (
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 1 ||
		header_magic != QOI_MAGIC ||
		desc->height >= QOI_PIXELS_MAX / desc->width
	) {
		return NULL;
	}

	if (channels == 0) {
		channels = desc->channels;
	}

	px_len = desc->width * desc->height * channels;
	pixels = (unsigned char *) QOI_MALLOC(px_len);
	if (!pixels) {
		return NULL;
	}

	QOI_ZEROARR(index);
	px.rgba.r = 0;
	px.rgba.g = 0;
	px.rgba.b = 0;
	px.rgba.a = 255;

	chunks_len = size - (int)sizeof(qoi_padding);
	for (px_pos = 0; px_pos < px_len;) {
		if (run > 0) {
			run--;
		}
		else if (p < chunks_len) {
			int b1 = bytes[p++];
			if (b1 < 0x80) {  		/* QOI_OP_LUMA */
				int vg = ((b1 >> 4) & 7) - 4;
				px.rgba.g += vg;
				if (vg < 0) {
					px.rgba.r += vg - 1 + ((b1 >> 2) & 3);
					px.rgba.b += vg - 1 +  (b1 &  3);
				}
				else {
					px.rgba.r += vg - 2 + ((b1 >> 2) & 3);
					px.rgba.b += vg - 2 +  (b1 &  3);
				}
				index[index_pos++ & 63] = px;
			}
			else if (b1 < 0xc0) {		/* QOI_OP_INDEX */
				px = index[b1 & 63];
			}
			else if (b1 < 0xe0) {		/* QOI_OP_LUMA2 */
				int b2 = bytes[p++];
				int vg = (b1 & 0x1f) - 16;
				px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
				px.rgba.g += vg;
				px.rgba.b += vg - 8 +  (b2       & 0x0f);
				index[index_pos++ & 63] = px;
			}
			else if (b1 < 0xf0) {		/* QOI_OP_LUMA3 */
				int b2 = bytes[p++];
				int vg = bytes[p++] - 128;
				px.rgba.r += vg - 32 + (((b1 & 0x0f) << 2) | ((b2 >> 6) & 3));
				px.rgba.g += vg;
				px.rgba.b += vg - 32 +   (b2 & 0x3f);
				index[index_pos++ & 63] = px;
			}
			else if (b1 < 0xf8) {		/* QOI_OP_RUN */
				run = b1 & 7;
			}
			else if (b1 < 0xfc) {		/* QOI_OP_RUN2 */
				run = ((b1 & 3) << 8) | bytes[p++];
			}
			else if (b1 == QOI_OP_GRAY) {
				int vg = bytes[p++];
				px.rgba.r = vg;
				px.rgba.g = vg;
				px.rgba.b = vg;
				index[index_pos++ & 63] = px;
			}
			else if (b1 == QOI_OP_RGB) {
				px.rgba.r = bytes[p++];
				px.rgba.g = bytes[p++];
				px.rgba.b = bytes[p++];
				index[index_pos++ & 63] = px;
			}
			else if (b1 == QOI_OP_A) {
				px.rgba.a = bytes[p++];
				continue;
			}
			else {				/* QOI_OP_END */
				break;
			}
		}

		pixels[px_pos + 0] = px.rgba.r;
		pixels[px_pos + 1] = px.rgba.g;
		pixels[px_pos + 2] = px.rgba.b;
		
		if (channels == 4) {
			pixels[px_pos + 3] = px.rgba.a;
		}
 		px_pos += channels;
	}

	return pixels;
}

#ifndef QOI_NO_STDIO
#include <stdio.h>

int qoi_write(const char *filename, const void *data, const qoi_desc *desc) {
	FILE *f = fopen(filename, "wb");
	int size;
	void *encoded;

	if (!f) {
		return 0;
	}

	encoded = qoi_encode(data, desc, &size);
	if (!encoded) {
		fclose(f);
		return 0;
	}

	fwrite(encoded, 1, size, f);
	fclose(f);

	QOI_FREE(encoded);
	return size;
}

void *qoi_read(const char *filename, qoi_desc *desc, int channels) {
	FILE *f = fopen(filename, "rb");
	int size, bytes_read;
	void *pixels, *data;

	if (!f) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);

	data = QOI_MALLOC(size);
	if (!data) {
		fclose(f);
		return NULL;
	}

	bytes_read = fread(data, 1, size, f);
	fclose(f);

	pixels = qoi_decode(data, bytes_read, desc, channels);
	QOI_FREE(data);
	return pixels;
}

#endif /* QOI_NO_STDIO */
#endif /* QOI_IMPLEMENTATION */

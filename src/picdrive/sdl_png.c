/*****************************************************************************
 * sdl_png.c - picdriver for loading PNG files through SDL_Image
 ****************************************************************************/

/*----------------------------------------------------------------------------
 * include the usual header files...
 *--------------------------------------------------------------------------*/

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <assert.h>
#include <memory.h>
#include <reqlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniz.h"

#include "jimk.h"
#include "sdl_pdr.h"

// ---------------------------------------------------------------------------
static SDL_Surface* sdlpdr_load_png(const char* path)
{
	SDL_Surface* loaded = IMG_Load(path);
	SDL_Surface* result = NULL;
	if (!loaded) {
		return NULL;
	}

	// Always convert to indexed color format for AnimatorPro
	if (loaded->format != SDL_PIXELFORMAT_INDEX8) {
		result = sdlpdr_convert_colors(loaded, 256);
		SDL_DestroySurface(loaded);
	} else {
		result = loaded;
	}

	return result;
}

// CRC table for PNG chunks
static const uint32_t crc_table[256] = {
	0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
	0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
	0x1DB71064, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
	0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA0F3D63, 0x8D080DF5,
	0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
	0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
	0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
	0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
	0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
	0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
	0x6B6B51F4, 0x1C6C6162, 0x856530D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
	0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD37CF3, 0xFBD44C65,
	0x4DB26158, 0x3AB551CE, 0xA3BC0074, 0xD4BB30E2, 0x4ADFA541, 0x3DD895D7, 0xA4D1C46D, 0xD3D6F4FB,
	0x4369E96A, 0x346ED9FC, 0xAD678846, 0xDA60B8D0, 0x44042D73, 0x33031DE5, 0xAA0A4C5F, 0xDD0D7CC9,
	0x5005713C, 0x270241AA, 0xBE0B1010, 0xC90C2086, 0x5768B525, 0x206F85B3, 0xB966D409, 0xCE61E49F,
	0x5EDEF90E, 0x29D9C998, 0xB0D09822, 0xC7D7A8B4, 0x59B33D17, 0x2EB40D81, 0xB7BD5C3B, 0xC0BA6CAD,
	0xEDB88320, 0x9ABFB3B6, 0x03B6E20C, 0x74B1D29A, 0xEAD54739, 0x9DD277AF, 0x04DB2615, 0x73DC1683,
	0xE3630B12, 0x94643B84, 0x0D6D6A3E, 0x7A6A5AA8, 0xE40ECF0B, 0x9309FF9D, 0x0A00AE27, 0x7D079EB1,
	0xF00F9344, 0x8708A3D2, 0x1E01F268, 0x6906C2FE, 0xF762575D, 0x806567CB, 0x196C3671, 0x6E6B06E7,
	0xFED41B76, 0x89D32BE0, 0x10DA7A5A, 0x67DD4ACC, 0xF9B9DF6F, 0x8EBEEFF9, 0x17B7BE43, 0x60B08ED5,
	0xD6D6A3E8, 0xA1D1937E, 0x38D8C2C4, 0x4FDFF252, 0xD1BB67F1, 0xA6BC5767, 0x3FB506DD, 0x48B2364B,
	0xD80D2BDA, 0xAF0A1B4C, 0x36034AF6, 0x41047A60, 0xDF60EFC3, 0xA867DF55, 0x316E8EEF, 0x4669BE79,
	0xCB61B38C, 0xBC66831A, 0x256FD2A0, 0x5268E236, 0xCC0C7795, 0xBB0B4703, 0x220216B9, 0x5505262F,
	0xC5BA3BBE, 0xB2BD0B28, 0x2BB45A92, 0x5CB36A04, 0xC2D7FFA7, 0xB5D0CF31, 0x2CD99E8B, 0x5BDEAE1D,
	0x9B64C2B0, 0xEC63F226, 0x756AA39C, 0x026D930A, 0x9C0906A9, 0xEB0E363F, 0x72076785, 0x05005713,
	0x95BF4A82, 0xE2B87A14, 0x7BB12BAE, 0x0CB61B38, 0x92D28E9B, 0xE5D5BE0D, 0x7CDCEFB7, 0x0BDBDF21,
	0x86D3D2D4, 0xF1D4E242, 0x68DDB3F8, 0x1FDA836E, 0x81BE16CD, 0xF6B9265B, 0x6FB077E1, 0x18B74777,
	0x88085AE6, 0xFF0F6A70, 0x66063BCA, 0x11010B5C, 0x8F659EFF, 0xF862AE69, 0x616BFFD3, 0x166CCF45,
	0xA00AE278, 0xD70DD2EE, 0x4E048354, 0x3903B3C2, 0xA7672661, 0xD06016F7, 0x4969474D, 0x3E6E77DB,
	0xAED16A4A, 0xD9D65ADC, 0x40DF0B66, 0x37D83BF0, 0xA9BCAE53, 0xDEBB9EC5, 0x47B2CF7F, 0x30B5FFE9,
	0xBDBDF21C, 0xCABAC28A, 0x53B39330, 0x24B4A3A6, 0xBAD03605, 0xCDD70693, 0x54DE5729, 0x23D967BF,
	0xB3667A2E, 0xC4614AB8, 0x5D681B02, 0x2A6F2B94, 0xB40BBE37, 0xC30C8EA1, 0x5A05DF1B, 0x2D02EF8D};

// Function to calculate CRC32 for PNG chunks
static uint32_t calculate_crc(const uint8_t* data, size_t length)
{
	uint32_t crc = 0xFFFFFFFF;

	for (size_t i = 0; i < length; i++) {
		crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
	}

	return crc ^ 0xFFFFFFFF;
}

// Write a 4-byte integer in big-endian format
static void write_uint32(uint8_t* buffer, uint32_t value)
{
	buffer[0] = (value >> 24) & 0xFF;
	buffer[1] = (value >> 16) & 0xFF;
	buffer[2] = (value >> 8) & 0xFF;
	buffer[3] = value & 0xFF;
}

static bool sdlpdr_save_png(SDL_Surface* surface, const char* path)
{
	FILE* fp = NULL;
	bool success = false;
	uint8_t* compressed_data = NULL;
	const bool write_alpha = vs.pic_write_alpha == PIC_IO_WRITE_ALPHA;

	// Ensure surface is INDEX8 type with valid palette
	if (!surface || surface->format != SDL_PIXELFORMAT_INDEX8) {
		return false;
	}

	SDL_Palette* palette = SDL_GetSurfacePalette(surface);
	if (!palette || palette->ncolors == 0 || palette->ncolors > 256) {
		return false;
	}

	// Open file for writing
	fp = fopen(path, "wb");
	if (!fp) {
		return false;
	}

	// Write PNG signature
	static const uint8_t png_signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
	if (fwrite(png_signature, 1, 8, fp) != 8) {
		goto cleanup;
	}

	// Prepare and write IHDR chunk
	uint8_t ihdr_data[13];
	write_uint32(ihdr_data, surface->w);      // Width
	write_uint32(ihdr_data + 4, surface->h);  // Height
	ihdr_data[8] = 8;                         // Bit depth (8 bits)
	ihdr_data[9] = 3;                         // Color type (indexed color)
	ihdr_data[10] = 0;                        // Compression method (deflate)
	ihdr_data[11] = 0;                        // Filter method (standard)
	ihdr_data[12] = 0;                        // Interlace method (none)

	// Write IHDR chunk length
	uint32_t chunk_length = 13;
	uint8_t length_bytes[4];
	write_uint32(length_bytes, chunk_length);
	if (fwrite(length_bytes, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Write IHDR chunk type
	const char* chunk_type = "IHDR";
	if (fwrite(chunk_type, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Write IHDR chunk data
	if (fwrite(ihdr_data, 1, chunk_length, fp) != chunk_length) {
		goto cleanup;
	}

	// Calculate and write IHDR chunk CRC
	uint8_t ihdr_crc_data[4 + 13];  // Type + data
	memcpy(ihdr_crc_data, chunk_type, 4);
	memcpy(ihdr_crc_data + 4, ihdr_data, 13);
	uint32_t ihdr_crc = calculate_crc(ihdr_crc_data, 4 + 13);
	uint8_t ihdr_crc_bytes[4];
	write_uint32(ihdr_crc_bytes, ihdr_crc);
	if (fwrite(ihdr_crc_bytes, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Prepare and write PLTE chunk
	chunk_length = palette->ncolors * 3;
	write_uint32(length_bytes, chunk_length);
	if (fwrite(length_bytes, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Write PLTE chunk type
	chunk_type = "PLTE";
	if (fwrite(chunk_type, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Write PLTE chunk data (palette colors)
	uint8_t* plte_data = (uint8_t*)malloc(chunk_length);
	if (!plte_data) {
		goto cleanup;
	}

	for (int i = 0; i < palette->ncolors; i++) {
		plte_data[i * 3] = palette->colors[i].r;
		plte_data[i * 3 + 1] = palette->colors[i].g;
		plte_data[i * 3 + 2] = palette->colors[i].b;
	}

	if (fwrite(plte_data, 1, chunk_length, fp) != chunk_length) {
		free(plte_data);
		goto cleanup;
	}

	// Calculate and write PLTE chunk CRC
	uint8_t* plte_crc_data = (uint8_t*)malloc(4 + chunk_length);
	if (!plte_crc_data) {
		free(plte_data);
		goto cleanup;
	}

	memcpy(plte_crc_data, chunk_type, 4);
	memcpy(plte_crc_data + 4, plte_data, chunk_length);
	uint32_t plte_crc = calculate_crc(plte_crc_data, 4 + chunk_length);
	uint8_t plte_crc_bytes[4];
	write_uint32(plte_crc_bytes, plte_crc);

	free(plte_crc_data);
	free(plte_data);

	if (fwrite(plte_crc_bytes, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Add tRNS chunk if alpha writing is enabled
	if (write_alpha) {
		// Write tRNS chunk for transparency of index 0
		uint8_t trans_data[256];

		// Set all palette entries to fully opaque (255)
		memset(trans_data, 255, 256);

		// Set palette index 0 to fully transparent (0)
		trans_data[0] = 0;

		// Use the actual palette size for the chunk length
		chunk_length = palette->ncolors;
		write_uint32(length_bytes, chunk_length);
		if (fwrite(length_bytes, 1, 4, fp) != 4) {
			goto cleanup;
		}

		// Write tRNS chunk type
		chunk_type = "tRNS";
		if (fwrite(chunk_type, 1, 4, fp) != 4) {
			goto cleanup;
		}

		// Write tRNS chunk data
		if (fwrite(trans_data, 1, chunk_length, fp) != chunk_length) {
			goto cleanup;
		}

		// Calculate and write tRNS chunk CRC
		uint8_t* trns_crc_data = (uint8_t*)malloc(4 + chunk_length);
		if (!trns_crc_data) {
			goto cleanup;
		}

		memcpy(trns_crc_data, chunk_type, 4);
		memcpy(trns_crc_data + 4, trans_data, chunk_length);
		uint32_t trns_crc = calculate_crc(trns_crc_data, 4 + chunk_length);
		uint8_t trns_crc_bytes[4];
		write_uint32(trns_crc_bytes, trns_crc);

		free(trns_crc_data);

		if (fwrite(trns_crc_bytes, 1, 4, fp) != 4) {
			goto cleanup;
		}
	}

	// Prepare image data for compression
	int stride = surface->pitch;
	uint8_t* pixels = (uint8_t*)surface->pixels;

	// Add filter byte (0) at the beginning of each scanline
	size_t filtered_size = surface->h * (stride + 1);
	uint8_t* filtered_data = (uint8_t*)malloc(filtered_size);
	if (!filtered_data) {
		goto cleanup;
	}

	for (int y = 0; y < surface->h; y++) {
		filtered_data[y * (stride + 1)] = 0;  // Filter type 0 (None)
		memcpy(filtered_data + y * (stride + 1) + 1, pixels + y * stride, stride);
	}

	// Compress image data using miniz
	size_t compressed_size = 0;
	compressed_data =
		(uint8_t*)tdefl_compress_mem_to_heap(filtered_data, filtered_size, &compressed_size,
											 TDEFL_DEFAULT_MAX_PROBES | TDEFL_WRITE_ZLIB_HEADER);
	free(filtered_data);

	if (!compressed_data) {
		goto cleanup;
	}

	// Write IDAT chunk
	write_uint32(length_bytes, (uint32_t)compressed_size);
	if (fwrite(length_bytes, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Write IDAT chunk type
	chunk_type = "IDAT";
	if (fwrite(chunk_type, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Write IDAT chunk data
	if (fwrite(compressed_data, 1, compressed_size, fp) != compressed_size) {
		goto cleanup;
	}

	// Calculate and write IDAT chunk CRC
	uint8_t* idat_crc_data = (uint8_t*)malloc(4 + compressed_size);
	if (!idat_crc_data) {
		goto cleanup;
	}

	memcpy(idat_crc_data, chunk_type, 4);
	memcpy(idat_crc_data + 4, compressed_data, compressed_size);
	uint32_t idat_crc = calculate_crc(idat_crc_data, 4 + compressed_size);
	uint8_t idat_crc_bytes[4];
	write_uint32(idat_crc_bytes, idat_crc);

	free(idat_crc_data);

	if (fwrite(idat_crc_bytes, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Write IEND chunk
	write_uint32(length_bytes, 0);  // Zero length
	if (fwrite(length_bytes, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Write IEND chunk type
	chunk_type = "IEND";
	if (fwrite(chunk_type, 1, 4, fp) != 4) {
		goto cleanup;
	}

	// Calculate and write IEND chunk CRC
	uint32_t iend_crc = calculate_crc((const uint8_t*)chunk_type, 4);
	uint8_t iend_crc_bytes[4];
	write_uint32(iend_crc_bytes, iend_crc);

	if (fwrite(iend_crc_bytes, 1, 4, fp) != 4) {
		goto cleanup;
	}

	success = true;

cleanup:
	if (compressed_data) {
		mz_free(compressed_data);
	}

	if (fp) {
		fclose(fp);
	}

	return success;
}

/*----------------------------------------------------------------------------
 * Setup rex & pdr interface structures...
 *--------------------------------------------------------------------------*/
static char long_description[] =
	"SDL_Image Loader: PNG Files\n\n"
	"All PNG files are saved as 8-bit.\n"
	"PNGs are quantized to 256 colors\n"
	"at load time as needed.";


SDL_PDR_CREATE(PNG, "PNG Format (SDL)", long_description, "png", sdlpdr_load_png, sdlpdr_save_png);

#include <SDL3/SDL.h>
#include <assert.h>
#include <float.h>  // for DBL_MAX
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "cmap.h"
#include "sdl_pdr.h"

void sdlpdr_cmap_to_palette(const Cmap* cmap, SDL_Palette* palette)
{
	assert(palette);
	assert(cmap);
	assert(palette->ncolors >= cmap->num_colors);

	for (int i = 0; i < 256; i++) {
		palette->colors[i].r = cmap->ctab[i].r;
		palette->colors[i].g = cmap->ctab[i].g;
		palette->colors[i].b = cmap->ctab[i].b;
		palette->colors[i].a = 0xFF;
	}
}

void sdlpdr_palette_to_cmap(const SDL_Palette* palette, Cmap* cmap)
{
	assert(palette);
	assert(cmap);
	assert(cmap->num_colors >= palette->ncolors);

	for (int i = 0; i < 256; i++) {
		cmap->ctab[i].r = palette->colors[i].r;
		cmap->ctab[i].g = palette->colors[i].g;
		cmap->ctab[i].b = palette->colors[i].b;
	}
}

/**
 * Convert any SDL surface to an 8-bit indexed color surface
 * This function is based on the convrgb.c code from the CONVERT
 * tool in the original source code.
 *
 * @param surface The source surface to convert
 * @param num_colors Maximum number of colors to use (between 2 and 256)
 * @return A new SDL_Surface in INDEX8 format or NULL on error
 */
SDL_Surface* sdlpdr_convert_colors(SDL_Surface* surface, size_t num_colors)
{
	if (surface == NULL || num_colors < 2 || num_colors > 256) {
		SDL_SetError("Invalid parameters for color conversion");
		return NULL;
	}

	// Create histogram and related structures for color quantization
	Uint8* histogram = NULL;
	Uint8* rgb_bufs[3] = {NULL, NULL, NULL};
	Uint8* linebuf = NULL;
	int width = surface->w;
	int height = surface->h;
	SDL_Surface* new_surface = NULL;
	SDL_Palette* palette = NULL;

	// Allocate histogram for color analysis
	size_t hist_size = (256 * 256 * 256) / 8;  // Same as HIST256_SIZE in convrgb.c
	histogram = (Uint8*)calloc(1, hist_size);
	if (!histogram) {
		SDL_SetError("Out of memory for histogram");
		goto cleanup;
	}

	// Allocate RGB buffers for processing
	rgb_bufs[0] = (Uint8*)malloc(width);  // R
	rgb_bufs[1] = (Uint8*)malloc(width);  // G
	rgb_bufs[2] = (Uint8*)malloc(width);  // B
	linebuf = (Uint8*)malloc(width * 3);  // RGB triplets

	if (!rgb_bufs[0] || !rgb_bufs[1] || !rgb_bufs[2] || !linebuf) {
		SDL_SetError("Out of memory for RGB buffers");
		goto cleanup;
	}

	// Lock surface for pixel reading
	if (!SDL_LockSurface(surface)) {
		SDL_SetError("Unable to lock source surface for color conversion");
		goto cleanup;
	}

	// Extract RGB values from the source surface and build histogram
	Uint8 r, g, b;
	int x, y, cbyte;
	long cbit;
	static const Uint8 bitmasks[8] = {0x80, 0x40, 0x20, 0x10, 0x8, 0x4, 0x2, 0x1};

	for (y = 0; y < height; y++) {
		// Extract a row of RGB values
		for (x = 0; x < width; x++) {
			if (SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, NULL) != true) {
				// Error reading pixel
				continue;
			}

			rgb_bufs[0][x] = r;
			rgb_bufs[1][x] = g;
			rgb_bufs[2][x] = b;

			// Set bits in the histogram
			cbit = ((long)r << 16) + (g << 8) + b;
			cbyte = cbit >> 3;
			histogram[cbyte] |= bitmasks[cbit & 7];
		}
	}

	SDL_UnlockSurface(surface);

	// Create the destination surface with INDEX8 format
	new_surface = SDL_CreateSurface(width, height, SDL_PIXELFORMAT_INDEX8);
	if (!new_surface) {
		SDL_SetError("Unable to create destination surface for color conversion");
		goto cleanup;
	}

	// Create a palette for the new surface
	palette = SDL_CreatePalette((int)num_colors);
	if (!palette) {
		SDL_SetError("Unable to create palette for color conversion");
		goto cleanup;
	}

	// Count unique colors in the histogram
	int unique_colors = 0;
	for (size_t i = 0; i < hist_size; i++) {
		Uint8 byte = histogram[i];
		while (byte) {
			if (byte & 1) {
				unique_colors++;
			}
			byte >>= 1;
		}
	}

	// Build color table from histogram
	Uint8* color_table = (Uint8*)malloc(unique_colors * 3);
	if (!color_table) {
		SDL_SetError("Out of memory for color table");
		goto cleanup;
	}

	int color_index = 0;
	for (int r = 0; r < 256; r++) {
		for (int g = 0; g < 256; g++) {
			for (int b = 0; b < 256;) {
				Uint8 byte = histogram[(((r << 16) | (g << 8) | b) >> 3)];
				Uint8 mask = 0x80;

				for (int bit = 0; bit < 8 && b < 256; bit++, b++, mask >>= 1) {
					if (byte & mask) {
						if (color_index < unique_colors) {
							color_table[color_index * 3 + 0] = r;
							color_table[color_index * 3 + 1] = g;
							color_table[color_index * 3 + 2] = b;
							color_index++;
						}
					}
				}
			}
		}
	}

	// Create palette colors from the color table
	// Use color quantization if we have more colors than our limit
	if (unique_colors <= num_colors) {
		// We can use all colors directly
		SDL_Color colors[256];
		for (int i = 0; i < unique_colors; i++) {
			colors[i].r = color_table[i * 3 + 0];
			colors[i].g = color_table[i * 3 + 1];
			colors[i].b = color_table[i * 3 + 2];
			colors[i].a = 0xFF;
		}
		SDL_SetPaletteColors(palette, colors, 0, unique_colors);
	} else {
		// Need to pack colors - use a basic clustering approach
		SDL_Color colors[256];
		int threshold = 32;  // Adjust this for quality vs color count tradeoff
		int used_colors = 0;

		// Use the first color as a starting point
		colors[0].r = color_table[0];
		colors[0].g = color_table[1];
		colors[0].b = color_table[2];
		colors[0].a = 255;
		used_colors = 1;

		// Find colors that are more than threshold distance away
		for (int i = 1; i < unique_colors && used_colors < (int)num_colors; i++) {
			Uint8 r = color_table[i * 3 + 0];
			Uint8 g = color_table[i * 3 + 1];
			Uint8 b = color_table[i * 3 + 2];

			// See if this color is already represented
			int min_dist = threshold * threshold;
			for (int j = 0; j < used_colors; j++) {
				int dr = r - colors[j].r;
				int dg = g - colors[j].g;
				int db = b - colors[j].b;
				int dist = dr * dr + dg * dg + db * db;

				if (dist < min_dist) {
					min_dist = dist;
				}
			}

			// If color is sufficiently different, add it
			if (min_dist >= threshold * threshold) {
				colors[used_colors].r = r;
				colors[used_colors].g = g;
				colors[used_colors].b = b;
				colors[used_colors].a = 255;
				used_colors++;
			}
		}

		SDL_SetPaletteColors(palette, colors, 0, used_colors);
	}

	// Set the palette on the new surface
	SDL_SetSurfacePalette(new_surface, palette);
	SDL_DestroyPalette(palette);  // Surface has taken ownership

	// Map each pixel from the source to the best matching color in the palette
	if (!SDL_LockSurface(new_surface)) {
		goto cleanup;
	}

	Uint8* dst_pixels = (Uint8*)new_surface->pixels;

	for (y = 0; y < height; y++) {
		for (x = 0; x < width; x++) {
			if (SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, NULL) != true) {
				continue;
			}

			// Find closest color in palette
			int closest_index = 0;
			unsigned int closest_diff = 765;  // 255*3, max possible diff

			for (int i = 0; i < palette->ncolors; i++) {
				unsigned int dr = abs((int)r - palette->colors[i].r);
				unsigned int dg = abs((int)g - palette->colors[i].g);
				unsigned int db = abs((int)b - palette->colors[i].b);
				unsigned int diff = dr + dg + db;

				if (diff < closest_diff) {
					closest_diff = diff;
					closest_index = i;

					if (diff == 0) {  // Perfect match
						break;
					}
				}
			}

			// Write the closest color index to the indexed surface
			dst_pixels[y * new_surface->pitch + x] = closest_index;
		}
	}

	SDL_UnlockSurface(new_surface);

	// Cleanup and return
	free(color_table);
	free(linebuf);
	free(rgb_bufs[0]);
	free(rgb_bufs[1]);
	free(rgb_bufs[2]);
	free(histogram);

	return new_surface;

cleanup:
	if (new_surface) {
		SDL_DestroySurface(new_surface);
	}
	if (palette) {
		SDL_DestroyPalette(palette);
	}
	if (histogram) {
		free(histogram);
	}
	if (linebuf) {
		free(linebuf);
	}
	if (rgb_bufs[0]) {
		free(rgb_bufs[0]);
	}
	if (rgb_bufs[1]) {
		free(rgb_bufs[1]);
	}
	if (rgb_bufs[2]) {
		free(rgb_bufs[2]);
	}

	return NULL;
}

SDL_Surface* sdlpdr_convert_surface_to_palette(const SDL_Surface* surface,
											   const SDL_Palette* palette)
{
	assert(surface);
	assert(palette);

	// Convert the surface to 8-bit indexed format with the specified palette
	SDL_Surface* indexedSurface = SDL_CreateSurface(surface->w, surface->h, SDL_PIXELFORMAT_INDEX8);
	if (!indexedSurface) {
		SDL_SetError("Indexed surface allocation failed.");
		return NULL;
	}

	if (!SDL_SetSurfacePalette(indexedSurface, palette)) {
		fprintf(stderr, "SDL_SetSurfacePalette failed: %s\n", SDL_GetError());
		SDL_DestroySurface(indexedSurface);
		return NULL;
	}

	if (!SDL_LockSurface(indexedSurface)) {
		fprintf(stderr, "SDL_LockSurface failed: %s\n", SDL_GetError());
		SDL_DestroySurface(indexedSurface);
		return NULL;
	}

	Uint8 r, g, b;
	for (int y = 0; y < surface->h; y += 1) {
		for (int x = 0; x < surface->w; x += 1) {
			SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, NULL);
			const double d_r = (float)r;
			const double d_g = (float)g;
			const double d_b = (float)b;
			double min_dist = DBL_MAX;
			uint8_t best_index = 0;

			for (int index = 0; index < palette->ncolors; index += 1) {
				// use perceptual weighting
				const double delta_r = ((double)palette->colors[index].r - d_r) * 0.3;
				const double delta_g = ((double)palette->colors[index].g - d_g) * 0.59;
				const double delta_b = ((double)palette->colors[index].b - d_b) * 0.11;

				double sq_dist = delta_r * delta_r + delta_g * delta_g + delta_b * delta_b;
				if (sq_dist < min_dist) {
					min_dist = sq_dist;
					best_index = index;
				}
			}

			uint8_t* surface_buf = (uint8_t*)indexedSurface->pixels;
			surface_buf[y * indexedSurface->pitch + x] = best_index;
		}
	}

	SDL_UnlockSurface(indexedSurface);
	return indexedSurface;
}

/*
 * kiki note:
 * Conversion using LAB color and Delta E 2000 calculation.
 * Not using it right now, but leaving it here in case I get
 * curious enough to come back to it.
 */

/*
// Define a simple Lab color struct
typedef struct {
	double L;
	double a;
	double b;
} LabColor;

// Function to convert RGB (0-255 range) to CIELAB
static void RGBtoLab(int R, int G, int B, LabColor *lab)
{
	// Normalize and convert to a linear scale
	double r = R / 255.0, g = G / 255.0, b = B / 255.0;
	r = (r > 0.04045) ? pow((r + 0.055) / 1.055, 2.4) : (r / 12.92);
	g = (g > 0.04045) ? pow((g + 0.055) / 1.055, 2.4) : (g / 12.92);
	b = (b > 0.04045) ? pow((b + 0.055) / 1.055, 2.4) : (b / 12.92);
	r *= 100.0;
	g *= 100.0;
	b *= 100.0;

	// Convert to XYZ using the sRGB matrix for D65 illuminant
	double X = r * 0.4124 + g * 0.3576 + b * 0.1805;
	double Y = r * 0.2126 + g * 0.7152 + b * 0.0722;
	double Z = r * 0.0193 + g * 0.1192 + b * 0.9505;

	// Normalize for the D65 white point
	double Xn = 95.047, Yn = 100.000, Zn = 108.883;
	double x = X / Xn;
	double y = Y / Yn;
	double z = Z / Zn;

	// Helper constants and conversion to Lab
	double epsilon = 0.008856;
	double kappa = 903.3;
	double fx = (x > epsilon) ? cbrt(x) : ((kappa * x + 16) / 116);
	double fy = (y > epsilon) ? cbrt(y) : ((kappa * y + 16) / 116);
	double fz = (z > epsilon) ? cbrt(z) : ((kappa * z + 16) / 116);

	lab->L = (116 * fy) - 16;
	lab->a = 500 * (fx - fy);
	lab->b = 200 * (fy - fz);
}

// Helper function to convert degrees to radians
static inline double deg2rad(double deg)
{
	return deg * (M_PI / 180.0);
}

// Delta E 2000 calculation between two Lab colors
static double deltaE2000(LabColor lab1, LabColor lab2)
{
	double L1 = lab1.L, a1 = lab1.a, b1 = lab1.b;
	double L2 = lab2.L, a2 = lab2.a, b2 = lab2.b;

	// Compute chroma for both colors
	double C1 = sqrt(a1 * a1 + b1 * b1);
	double C2 = sqrt(a2 * a2 + b2 * b2);
	double avgC = (C1 + C2) / 2.0;

	// Compute G factor
	double G = 0.5 * (1.0 - sqrt(pow(avgC, 7) / (pow(avgC, 7) + pow(25.0, 7))));
	// Adjusted a values
	double a1Prime = a1 * (1.0 + G);
	double a2Prime = a2 * (1.0 + G);
	// Recalculate chroma with the adjusted a values
	double C1Prime = sqrt(a1Prime * a1Prime + b1 * b1);
	double C2Prime = sqrt(a2Prime * a2Prime + b2 * b2);

	// Calculate hue angles in degrees
	double h1Prime = (b1 == 0 && a1Prime == 0) ? 0 : atan2(b1, a1Prime) * 180.0 / M_PI;
	if (h1Prime < 0) {
		h1Prime += 360.0;
	}
	double h2Prime = (b2 == 0 && a2Prime == 0) ? 0 : atan2(b2, a2Prime) * 180.0 / M_PI;
	if (h2Prime < 0) {
		h2Prime += 360.0;
	}

	// Compute differences in L and C
	double deltaLPrime = L2 - L1;
	double deltaCPrime = C2Prime - C1Prime;

	// Compute difference in hue
	double hDiff = h2Prime - h1Prime;
	double deltaHPrime;
	if (C1Prime * C2Prime == 0) {
		deltaHPrime = 0;
	} else {
		if (fabs(hDiff) <= 180.0) {
			deltaHPrime = hDiff;
		} else if (hDiff > 180.0) {
			deltaHPrime = hDiff - 360.0;
		} else {
			deltaHPrime = hDiff + 360.0;
		}
	}
	deltaHPrime = 2.0 * sqrt(C1Prime * C2Prime) * sin(deg2rad(deltaHPrime) / 2.0);

	// Average values for L, C, and h
	double avgLPrime = (L1 + L2) / 2.0;
	double avgCPrime = (C1Prime + C2Prime) / 2.0;

	double hPrimeSum = h1Prime + h2Prime;
	double avgHPrime;
	if (C1Prime * C2Prime == 0) {
		avgHPrime = hPrimeSum;
	} else {
		if (fabs(h1Prime - h2Prime) <= 180.0) {
			avgHPrime = hPrimeSum / 2.0;
		} else {
			if (hPrimeSum < 360.0) {
				avgHPrime = (hPrimeSum + 360.0) / 2.0;
			} else {
				avgHPrime = (hPrimeSum - 360.0) / 2.0;
			}
		}
	}

	// T factor
	double T = 1.0 - 0.17 * cos(deg2rad(avgHPrime - 30.0)) + 0.24 * cos(deg2rad(2.0 * avgHPrime)) +
			   0.32 * cos(deg2rad(3.0 * avgHPrime + 6.0)) -
			   0.20 * cos(deg2rad(4.0 * avgHPrime - 63.0));

	// Delta theta in degrees
	double deltaTheta = 30.0 * exp(-pow((avgHPrime - 275.0) / 25.0, 2));

	double R_C = 2.0 * sqrt(pow(avgCPrime, 7) / (pow(avgCPrime, 7) + pow(25.0, 7)));
	double S_L = 1.0 + (0.015 * pow(avgLPrime - 50.0, 2)) / sqrt(20.0 + pow(avgLPrime - 50.0, 2));
	double S_C = 1.0 + 0.045 * avgCPrime;
	double S_H = 1.0 + 0.015 * avgCPrime * T;
	double R_T = -sin(deg2rad(2.0 * deltaTheta)) * R_C;

	// Final Delta E 2000 computation
	double deltaE =
		sqrt(pow(deltaLPrime / S_L, 2) + pow(deltaCPrime / S_C, 2) + pow(deltaHPrime / S_H, 2) +
			 R_T * (deltaCPrime / S_C) * (deltaHPrime / S_H));

	return deltaE;
}

SDL_Surface *sdlpdr_convert_surface_to_palette_lab_delta_e(const SDL_Surface *surface,
											   const SDL_Palette *palette)
{
	SDL_Surface *new_surface = NULL;
	int x, y;
	Uint8 r, g, b;
	int closest_index;
	unsigned int closest_diff;

	if (!surface || !palette) {
		return NULL;
	}

	new_surface = SDL_CreateSurface(surface->w, surface->h, SDL_PIXELFORMAT_INDEX8);
	if (!new_surface) {
		fprintf(stderr, "Unable to allocate new surface: %s\n", SDL_GetError());
		return NULL;
	}

	// Duplicate the palette for now
	SDL_Palette *new_palette = SDL_CreateSurfacePalette(new_surface);
	if (!new_palette) {
		fprintf(stderr, "Unable to allocate new palette for surface: %s\n", SDL_GetError());
		return NULL;
	}

	SDL_SetPaletteColors(new_palette, palette->colors, 0, palette->ncolors);
	SDL_SetSurfacePalette(new_surface, palette);

	if (SDL_LockSurface(new_surface) < 0) {
		fprintf(stderr, "Unable to lock new surface: %s\n", SDL_GetError());
		SDL_DestroySurface(new_surface);
		return NULL;
	}

	Uint8 *dst_pixels = (Uint8 *)new_surface->pixels;

	// pre-calculate lab colors
	LabColor palette_lab[256];
	for (int i = 0; i < 256; i++) {
		RGBtoLab(palette->colors[i].r, palette->colors[i].g, palette->colors[i].b, &palette_lab[i]);
	}

	for (int y = 0; y < surface->h; y++) {
		for (int x = 0; x < surface->w; x++) {
			int r, g, b;
			if (SDL_ReadSurfacePixel(surface, x, y, &r, &g, &b, NULL) != true) {
				continue;
			}

			// Check if an exact RGB match exists in the palette
			int exact_index = -1;
			for (int i = 0; i < palette->ncolors; i++) {
				if (r == palette->colors[i].r && g == palette->colors[i].g &&
					b == palette->colors[i].b) {
					exact_index = i;
					break;
				}
			}
			if (exact_index != -1) {
				dst_pixels[y * new_surface->pitch + x] = exact_index;
				continue;
			}

			// Convert the pixel color to Lab
			LabColor pixelLab;
			RGBtoLab(r, g, b, &pixelLab);

			// Find the closest color in the palette using Delta E 2000
			double minDeltaE = DBL_MAX;
			int closest_index = 0;
			for (int i = 0; i < palette->ncolors; i++) {
				double delta = deltaE2000(pixelLab, palette_lab[i]);
				if (delta < minDeltaE) {
					minDeltaE = delta;
					closest_index = i;
				}
			}

			// Write the chosen palette index to the new indexed surface
			dst_pixels[y * new_surface->pitch + x] = closest_index;
		}
	}

	SDL_UnlockSurface(new_surface);

	return new_surface;
}
*/
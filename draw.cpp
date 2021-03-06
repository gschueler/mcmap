/**
 * This file contains functions to create and draw to a bitmap image
 */

#include "draw.h"
#include "helper.h"
#include "colors.h"
#include "globals.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

#pragma pack(1)

typedef struct {
	int8_t Type[2];
	uint8_t Size[4];
	uint8_t Reserved[4];
	uint8_t DataOffset[4];
} BITMAP_FILEHEADER;

typedef struct {
	uint8_t HeaderSize[4];
	uint8_t Width[4];
	uint8_t Height[4];
	uint8_t Planes[2];
	uint8_t BitCount[2];
	uint8_t Compression[4];
	uint8_t ImageByteCount[4];
	uint8_t PelsPerMeterX[4];
	uint8_t PelsPerMeterY[4];
	uint8_t ClrUsed[4];
	uint8_t ClrImportant[4];
} BITMAP_INFOHEADER;

#define PIXEL(x,y) (gBitmap[(x) * 3 + (gBmpLocalHeight - ((y) + 1)) * gBmpLocalLineWidth])

namespace {
	uint8_t *gBitmap = NULL;
	int gBmpLocalLineWidth = 0, gBmpLocalWidth = 0, gBmpLocalHeight = 0, gBmpLocalX = 0, gBmpLocalY = 0;
	int gBmpLineWidth = 0, gBmpWidth = 0, gBmpHeight = 0;
	int64_t gBmpSize = 0, gBmpLocalSize = 0;

	inline void blend(uint8_t* c1, const uint8_t* c2);
	inline void modColor(uint8_t* color, const int mod);
	inline void addColor(uint8_t* color, uint8_t* add);

	// Split them up so setPixelBmp won't be one hell of a mess
	void setSnow(const size_t &x, const size_t &y, const uint8_t *color);
	void setTorch(const size_t &x, const size_t &y, const uint8_t *color);
	void setFlower(const size_t &x, const size_t &y, const uint8_t *color);
	void setFire(const size_t &x, const size_t &y, uint8_t *color, uint8_t *light, uint8_t *dark);
	void setGrass(const size_t &x, const size_t &y, const uint8_t *color, const uint8_t *light, const uint8_t *dark, const int &sub);
	void setFence(const size_t &x, const size_t &y, const uint8_t *color);
	void setStep(const size_t &x, const size_t &y, const uint8_t *color, const uint8_t *light, const uint8_t *dark);

	inline void le32(uint8_t* target, uint32_t val)
	{
		target[0] = uint8_t(val & 0xff);
		target[1] = uint8_t((val >> 8) & 0xff);
		target[2] = uint8_t((val >> 16) & 0xff);
		target[3] = uint8_t((val >> 24) & 0xff);
	}
	inline void le16(uint8_t* target, uint16_t val)
	{
		target[0] = uint8_t(val & 0xff);
		target[1] = uint8_t((val >> 8) & 0xff);
	}

	bool writeBitmapHeader24(FILE* fh, const size_t width, const size_t height)
	{
		//uint32_t datasize = ((uint32_t(width) * 3u + 3u) & ~uint32_t(3)) * uint32_t(height);
		BITMAP_FILEHEADER header;
		BITMAP_INFOHEADER info;
		memset(&header, 0, sizeof(header));
		memset(&info, 0, sizeof(info));
		header.Type[0] = 'B';
		header.Type[1] = 'M';
		le32(header.DataOffset, uint32_t(sizeof(header) + sizeof(info)));
		le32(info.HeaderSize, uint32_t(sizeof(info)));
		le16(info.BitCount, 24);
		le32(info.Height, uint32_t(height));
		le32(info.Width, uint32_t(width));
		le16(info.Planes, 1);
		return
				(fwrite(&header, 1, sizeof(header), fh) == sizeof(header))
				&& (fwrite(&info, 1, sizeof(info), fh) == sizeof(info));
	}
}

bool createImageBmp(FILE* fh, size_t width, size_t height, bool splitUp)
{
	gBmpWidth = (int)width;
	gBmpHeight = (int)height;
	gBmpLineWidth = int(gBmpWidth * 3 + 3) & ~int(3); // The size in bytes of a line in a bitmap has to be a multiple of 4
	gBmpSize = gBmpLineWidth * gBmpHeight;
	printf("Bitmap dimensions are %dx%d, 24bpp, %.2fMiB\n", gBmpWidth, gBmpHeight, float(gBmpSize / float(1024 * 1024)));
	fseek64(fh, 0, SEEK_SET);
	if (!writeBitmapHeader24(fh, width, height)) return false;
	if (splitUp) {
		// Pre allocate disk space with zeroes
		// Most OSes should automatically do that when seeking
		// beyond the EOF, but just to be sure, do it manually
		uint8_t *tmpdata = new uint8_t[gBmpLineWidth];
		memset(tmpdata, 0, gBmpLineWidth);
		for (int i = 0; i < gBmpHeight; ++i) {
			if ((int)fwrite(tmpdata, 1, gBmpLineWidth, fh) != gBmpLineWidth) return false;
		}
		delete[] tmpdata;
	} else {
		gBitmap = new uint8_t[gBmpSize];
		memset(gBitmap, 0, gBmpSize);
		gBmpLocalHeight = gBmpHeight;
		gBmpLocalLineWidth = gBmpLineWidth;
		gBmpLocalWidth = gBmpWidth;
	}
	return true;
}

bool saveImageBmp(FILE* fh)
{
	return fwrite(gBitmap, 1, gBmpSize, fh) == (size_t)gBmpSize;
}

bool loadImagePartBmp(FILE* fh, int startx, int starty, int width, int height)
{
	const int offX = MAX(0, -startx);
	const int offY = MAX(0, -starty);
	if (width + startx > gBmpWidth) width = gBmpWidth - startx;
	if (height + starty > gBmpHeight) height = gBmpHeight - starty;
	gBmpLocalWidth = width;
	gBmpLocalLineWidth = width * 3;
	const int readLineWidth = (width - offX) * 3;
	gBmpLocalHeight = height;
	gBmpLocalX = startx;
	gBmpLocalY = starty;
	printf("* Loading area at %d, %d of size %d x %d\n", int(startx), int(starty), int(width), int(height));
	if (gBitmap == NULL) {
		// First call, no image created yet, just alloc mem
		gBmpLocalSize = gBmpLocalLineWidth * gBmpHeight;
		gBitmap = new uint8_t[gBmpLocalSize];
		memset(gBitmap, 0, gBmpLocalSize);
	} else {
		// Need to load the area to render to from file, as it might contain some partially rendered stuff
		if (gBmpLocalSize < gBmpLocalLineWidth * gBmpHeight) {
			gBmpLocalSize = gBmpLocalLineWidth * gBmpHeight;
			delete[] gBitmap;
			gBitmap = new uint8_t[gBmpLocalSize];
		}
		int fileLine = gBmpHeight - gBmpLocalY - gBmpLocalHeight;
		for (int arrayLine = 0; arrayLine < gBmpLocalHeight - offY; ++arrayLine) {
			const int64_t pos = int64_t(fileLine) * int64_t(gBmpLineWidth) // row
					+ int64_t((gBmpLocalX + offX) * 3 // column
					+ sizeof(BITMAP_INFOHEADER) + sizeof(BITMAP_FILEHEADER)); // header
			fseek64(fh, pos, SEEK_SET);
			if ((int)fread(gBitmap + (arrayLine * gBmpLocalLineWidth) + offX * 3, 1, readLineWidth, fh) != readLineWidth) return false;
			++fileLine;
		}
	}
	return true;
}

bool saveImagePartBmp(FILE* fh)
{
	const int offX = MAX(0, -gBmpLocalX);
	const int offY = MAX(0, -gBmpLocalY);
	const int writeLineWidth = (gBmpLocalWidth - offX) * 3;
	size_t fileLine = gBmpHeight - gBmpLocalY - gBmpLocalHeight;
	for (int arrayLine = 0; arrayLine < gBmpLocalHeight - offY; ++arrayLine) {
		const int64_t pos = int64_t(fileLine) * int64_t(gBmpLineWidth)
				+ int64_t((gBmpLocalX + offX) * 3
				+ sizeof(BITMAP_INFOHEADER) + sizeof(BITMAP_FILEHEADER));
		if (pos < 0) continue;
		fseek64(fh, pos, SEEK_SET);
		if ((int)fwrite(gBitmap + (arrayLine * gBmpLocalLineWidth) + offX * 3, 1, writeLineWidth, fh) != writeLineWidth) return false;
		++fileLine;
	}
	return true;
}

size_t calcImageSizeBmp(int mapChunksX, int mapChunksZ, size_t mapHeight, int &pixelsX, int &pixelsY, bool tight)
{
	pixelsX = (mapChunksX * CHUNKSIZE_X + mapChunksZ * CHUNKSIZE_Z) * 2 + (tight ? 3 : 10);
	pixelsY = (mapChunksX * CHUNKSIZE_X + mapChunksZ * CHUNKSIZE_Z + (int)mapHeight * 2) + (tight ? 3 : 10);
	return (size_t(pixelsX * 3 + 3) & ~size_t(3)) * pixelsY;
}

void setPixelBmp(size_t x, size_t y, uint8_t color, float fsub)
{
	// Sets pixels around x,y where A is the anchor
	// T = given color, D = darker, L = lighter
	// A T T T
	// D D L L
	// D D L L
	//	  D L
	// First determine how much the color has to be lightened up or darkened
	int sub = int(fsub * (float(colors[color][BRIGHTNESS]) / 323.0f + .21f)); // The brighter the color, the stronger the impact
	uint8_t L[4], D[4], c[4];
	// Now make a local copy of the color that we can modify just for this one block
	memcpy(c, colors[color], 4);
	modColor(c, sub);
	// Then check the block type, as some types will be drawn differently
	if (color == SNOW) {
		setSnow(x, y, c);
		return;
	}
	if (color == TORCH || color == REDTORCH_ON || color == REDTORCH_OFF) {
		setTorch(x, y, c);
		return;
	}
	if (color == FLOWERR || color == FLOWERY || color == MUSHROOMB || color == MUSHROOMR) {
		setFlower(x, y, c);
		return;
	}
	if (color == FENCE) {
		setFence(x, y, c);
		return;
	}
	// All the above blocks didn't need the shaded down versions of the color, so we only calc them here
	// They are for the sides of blocks
	memcpy(L, c, 4);
	memcpy(D, c, 4);
	modColor(L, -17);
	modColor(D, -27);
	// A few more blocks with special handling... Those need the two colors we just mixed
	if (color == GRASS) {
		setGrass(x, y, c, L, D, sub);
		return;
	}
	if (color == FIRE) {
		setFire(x, y, c, L, D);
		return;
	}
	if (color == STEP) {
		setStep(x, y, c, L, D);
		return;
	}
	// In case the user wants noise, calc the strength now, depending on the desired intensity and the block's brightness
	int noise = 0;
	if (g_Noise && colors[color][NOISE]) {
		noise = int(float(g_Noise * colors[color][NOISE]) * (float(GETBRIGHTNESS(c) + 10) / 2650.0f));
	}
	// Ordinary blocks are all rendered the same way
	if (c[ALPHA] == 255) { // Fully opaque - faster
		// Top row
		uint8_t *pos = &PIXEL(x, y);
		for (size_t i = 0; i < 4; ++i, pos += 3) {
			memcpy(pos, c, 3);
			if (noise) modColor(pos, rand() % (noise * 2) - noise);
		}
		// Second row
		pos = &PIXEL(x, y+1);
		for (size_t i = 0; i < 4; ++i, pos += 3) {
			memcpy(pos, (i < 2 ? D : L), 3);
			// The weird check here is to get the pattern right, as the noise should be stronger
			// every other row, but take into account the isometric perspective
			if (noise) modColor(pos, rand() % (noise * 2) - noise * (i == 0 || i == 3 ? 1 : 2));
		}
		// Third row
		pos = &PIXEL(x, y+2);
		for (size_t i = 0; i < 4; ++i, pos += 3) {
			memcpy(pos, (i < 2 ? D : L), 3);
			if (noise) modColor(pos, rand() % (noise * 2) - noise * (i == 0 || i == 3 ? 2 : 1));
		}
		// Last row
		pos = &PIXEL(x, y+3);
		memcpy(pos+=3, D, 3);
		if (noise) modColor(pos, -(rand() % noise) * 2);
		memcpy(pos+=3, L, 3);
		if (noise) modColor(pos, -(rand() % noise) * 2);
	} else { // Not opaque, use slower blending code
		// Top row
		uint8_t *pos = &PIXEL(x, y);
		for (size_t i = 0; i < 4; ++i, pos += 3) {
			blend(pos, c);
			if (noise) modColor(pos, rand() % (noise * 2) - noise);
		}
		// Second row
		pos = &PIXEL(x, y+1);
		for (size_t i = 0; i < 4; ++i, pos += 3) {
			blend(pos, (i < 2 ? D : L));
			if (noise) modColor(pos, rand() % (noise * 2) - noise * (i == 0 || i == 3 ? 1 : 2));
		}
		// Third row
		pos = &PIXEL(x, y+2);
		for (size_t i = 0; i < 4; ++i, pos += 3) {
			blend(pos, (i < 2 ? D : L));
			if (noise) modColor(pos, rand() % (noise * 2) - noise * (i == 0 || i == 3 ? 2 : 1));
		}
		// Last row
		pos = &PIXEL(x, y+3);
		blend(pos+=3, D);
		if (noise) modColor(pos, -(rand() % noise) * 2);
		blend(pos+=3, L);
		if (noise) modColor(pos, -(rand() % noise) * 2);
	}
	// The above two branches are almost the same, maybe one could just create a function pointer and...
}

void blendPixelBmp(size_t x, size_t y, uint8_t color, float fsub)
{
	// Sets pixels around x,y where A is the anchor
	// T = given color, D = darker, L = lighter
	// A T T T
	// D D L L
	// D D L L
	//	  D L
	uint8_t L[4], D[4], c[4];
	// Now make a local copy of the color that we can modify just for this one block
	memcpy(c, colors[color], 4);
	c[ALPHA] = clamp(int(float(c[ALPHA]) * fsub)); // The brighter the color, the stronger the impact
	// They are for the sides of blocks
	memcpy(L, c, 4);
	memcpy(D, c, 4);
	modColor(L, -17);
	modColor(D, -27);
	// In case the user wants noise, calc the strength now, depending on the desired intensity and the block's brightness
	int noise = 0;
	if (g_Noise && colors[color][NOISE]) {
		noise = int(float(g_Noise * colors[color][NOISE]) * (float(GETBRIGHTNESS(c) + 10) / 2650.0f));
	}
	// Top row
	uint8_t *pos = &PIXEL(x, y);
	for (size_t i = 0; i < 4; ++i, pos += 3) {
		blend(pos, c);
		if (noise) modColor(pos, rand() % (noise * 2) - noise);
	}
	// Second row
	pos = &PIXEL(x, y+1);
	for (size_t i = 0; i < 4; ++i, pos += 3) {
		blend(pos, (i < 2 ? D : L));
		if (noise) modColor(pos, rand() % (noise * 2) - noise * (i == 0 || i == 3 ? 1 : 2));
	}
	/*
	// Third row
	pos = &PIXEL(x, y+2);
	for (size_t i = 0; i < 4; ++i, pos += 3) {
		addColor(pos, (i < 2 ? D : L));
		if (noise) modColor(pos, rand() % (noise * 2) - noise * (i == 0 || i == 3 ? 2 : 1));
	}
	// Last row
	pos = &PIXEL(x, y+3);
	addColor(pos+=3, D);
	if (noise) modColor(pos, -(rand() % noise) * 2);
	addColor(pos+=3, L);
	if (noise) modColor(pos, -(rand() % noise) * 2);
	*/
}

namespace {

	inline void blend(uint8_t* c1, const uint8_t* c2)
	{
		const float v2 = (float(c2[ALPHA]) / 255.0f);
		const float v1 = (1.0f - v2);
		c1[0] = uint8_t(float(c1[0]) * v1 + float(c2[0]) * v2);
		c1[1] = uint8_t(float(c1[1]) * v1 + float(c2[1]) * v2);
		c1[2] = uint8_t(float(c1[2]) * v1 + float(c2[2]) * v2);
	}

	inline void modColor(uint8_t* color, const int mod)
	{
		color[0] = clamp(color[0] + mod);
		color[1] = clamp(color[1] + mod);
		color[2] = clamp(color[2] + mod);
	}

	inline void addColor(uint8_t* color, uint8_t* add)
	{
		const float v2 = (float(add[ALPHA]) / 255.0f);
		const float v1 = (1.0f - (v2 * .2f));
		color[0] = clamp(uint16_t(float(color[0]) * v1 + float(add[0]) * v2));
		color[1] = clamp(uint16_t(float(color[1]) * v1 + float(add[1]) * v2));
		color[2] = clamp(uint16_t(float(color[2]) * v1 + float(add[2]) * v2));
	}

	void setSnow(const size_t &x, const size_t &y, const uint8_t *color)
	{
		// Top row (second row)
		uint8_t *pos = &PIXEL(x, y+1);
		for (size_t i = 0; i < 10; i += 3) {
			memcpy(pos+i, color, 3);
		}
		/*
		// Third row
		// This gives you white edges on height diffs, but I think
		// the current way looks closer to ingame, although trees
		// turn out a little prettier when using this imo
		pos = &PIXEL(x, y+2);
		memcpy(pos, D, 3);
		memcpy(pos+3, D, 3);
		memcpy(pos+6, L, 3);
		memcpy(pos+9, L, 3);
		*/
	}

	void setTorch(const size_t &x, const size_t &y, const uint8_t *color)
	{ // Maybe the orientation should be considered when drawing, but it probably isn't worth the efford
		uint8_t *pos = &PIXEL(x+2, y+1);
		memcpy(pos, color, 3);
		pos = &PIXEL(x+2, y+2);
		memcpy(pos, color, 3);
	}

	void setFlower(const size_t &x, const size_t &y, const uint8_t *color)
	{
		uint8_t *pos = &PIXEL(x, y+1);
		memcpy(pos+3, color, 3);
		memcpy(pos+9, color, 3);
		pos = &PIXEL(x+2, y+2);
		memcpy(pos, color, 3);
		pos = &PIXEL(x+1, y+3);
		memcpy(pos, color, 3);
	}

	void setFire(const size_t &x, const size_t &y, uint8_t *color, uint8_t *light, uint8_t *dark)
	{	// This basically just leaves out a few pixels
		// Top row
		uint8_t *pos = &PIXEL(x, y);
		for (size_t i = 0; i < 10; i += 6) {
			blend(pos+i, color);
		}
		// Second and third row
		for (size_t i = 1; i < 3; ++i) {
			pos = &PIXEL(x, y+i);
			blend(pos, dark);
			blend(pos+(3*i), dark);
			blend(pos+9, light);
		}
		// Last row
		pos = &PIXEL(x, y+3);
		blend(pos+6, light);
	}

	void setGrass(const size_t &x, const size_t &y, const uint8_t *color, const uint8_t *light, const uint8_t *dark, const int &sub)
	{	// this will make grass look like dirt from the side
		uint8_t L[4], D[4];
		memcpy(L, colors[DIRT], 4);
		memcpy(D, colors[DIRT], 4);
		modColor(L, sub - 15);
		modColor(D, sub - 25);
		// consider noise
		int noise = 0;
		if (g_Noise && colors[GRASS][NOISE]) {
			noise = int(float(g_Noise * colors[GRASS][NOISE]) * (float(GETBRIGHTNESS(color) + 10) / 2650.0f));
		}
		// Top row
		uint8_t *pos = &PIXEL(x, y);
		for (size_t i = 0; i < 4; ++i, pos += 3) {
			memcpy(pos, color, 3);
			if (noise) modColor(pos, rand() % (noise * 2) - noise);
		}
		// Second row
		pos = &PIXEL(x, y+1);
		memcpy(pos, dark, 3);
		memcpy(pos+3, dark, 3);
		memcpy(pos+6, light, 3);
		memcpy(pos+9, light, 3);
		// Third row
		pos = &PIXEL(x, y+2);
		memcpy(pos, D, 3);
		memcpy(pos+3, D, 3);
		memcpy(pos+6, L, 3);
		memcpy(pos+9, L, 3);
		// Last row
		pos = &PIXEL(x, y+3);
		memcpy(pos+3, D, 3);
		memcpy(pos+6, L, 3);
	}

	void setFence(const size_t &x, const size_t &y, const uint8_t *color)
	{
		// First row
		uint8_t *pos = &PIXEL(x, y);
		blend(pos, color);
		blend(pos+3, color);
		// Second row
		pos = &PIXEL(x, y+1);
		blend(pos, color);
		// Third row
		pos = &PIXEL(x, y+2);
		blend(pos, color);
		blend(pos+3, color);
		// Last row
		pos = &PIXEL(x, y+3);
		blend(pos, color);
	}

	void setStep(const size_t &x, const size_t &y, const uint8_t *color, const uint8_t *light, const uint8_t *dark)
	{
		uint8_t *pos = &PIXEL(x, y+2);
		for (size_t i = 0; i < 10; i += 3) {
			memcpy(pos+i, color, 3);
		}
		pos = &PIXEL(x, y+3);
		memcpy(pos+3, dark, 3);
		memcpy(pos+6, light, 3);
	}

}

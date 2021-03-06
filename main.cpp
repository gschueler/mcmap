/***
 * mcmap - create isometric maps of your minecraft alpha world
 * v1.7, 10-2010 by Zahl
 */

#define VERSION "1.7.0.5.7.15"

#include "helper.h"
#include "draw.h"
#ifdef WITHPNG
#include "draw_png.h"
#endif
#include "colors.h"
#include "worldloader.h"
#include "globals.h"
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#ifdef _DEBUG
#include <cassert>
#endif

using std::string;

namespace {
	// For bright edge
	bool gAtBottomLeft = true, gAtBottomRight = true;
	int gTotalFromChunkX, gTotalFromChunkZ, gTotalToChunkX, gTotalToChunkZ;
	bool gPng = false;

	bool (*createImage)(FILE* fh, size_t width, size_t height, bool splitUp) = NULL;
	bool (*saveImage)(FILE* fh) = NULL;
	bool (*loadImagePart)(FILE* fh, int startx, int starty, int width, int height) = NULL;
	void (*setPixel)(size_t x, size_t y, uint8_t color, float fsub) = NULL;
	void (*blendPixel)(size_t x, size_t y, uint8_t color, float fsub) = NULL;
	bool (*saveImagePart)(FILE* fh) = NULL;
	size_t (*calcImageSize)(int mapChunksX, int mapChunksZ, size_t mapHeight, int &pixelsX, int &pixelsY, bool tight) = NULL;
}

// Macros to make code more readable
#define BLOCK_AT_MAPEDGE(x,z) (((z)+1 == g_MapsizeZ-CHUNKSIZE_Z && gAtBottomLeft) || ((x)+1 == g_MapsizeX-CHUNKSIZE_X && gAtBottomRight))

void optimizeTerrain();
inline void blockCulling(const size_t x, const size_t y, const size_t z, size_t &removed);
void undergroundMode(bool explore);
bool prepareNextArea(int splitX, int splitZ, int &bitmapStartX, int &bitmapStartY);
void assignFunctionPointers();
void printHelp(char* binary);

int main(int argc, char** argv)
{
	// ########## command line parsing ##########
	if (argc < 2) {
		printHelp(argv[0]);
		return 1;
	}
	bool wholeworld = false;
	char *filename = NULL, *outfile = NULL, *colorfile = NULL;
	size_t memlimit = 1800 * size_t(1024 * 1024);
	bool memlimitSet = false;

	// First, for the sake of backward compatibility, try to parse command line arguments the old way first
	if (argc >= 7
			&& isNumeric(argv[1]) && isNumeric(argv[2]) && isNumeric(argv[3]) && isNumeric(argv[4])) { // Specific area of world
		g_FromChunkX = atoi(argv[1]);
		g_FromChunkZ = atoi(argv[2]);
		g_ToChunkX	= atoi(argv[3])+1;
		g_ToChunkZ	= atoi(argv[4])+1;
		g_MapsizeY = atoi(argv[5]);
		filename = argv[6];
		if (argc > 7) {
			g_Nightmode = (atoi(argv[7]) == 1);
			g_Underground = (atoi(argv[7]) == 2);
		}
	} else if (argc == 3 && isNumeric(argv[2])) { // Whole world - old way
		filename = argv[1];
		g_Nightmode = (atoi(argv[2]) == 1);
		g_Underground = (atoi(argv[2]) == 2);
	} else { // -- New command line parsing --
#		define MOREARGS(x) (argpos + (x) < argc)
#		define NEXTARG argv[++argpos]
#		define POLLARG(x) argv[argpos + (x)]
		int argpos = 0;
		while (MOREARGS(1)) {
			const char *option = NEXTARG;
			if (strcmp(option, "-from") == 0) {
				if (!MOREARGS(2) || !isNumeric(POLLARG(1)) || !isNumeric(POLLARG(2))) {
					printf("Error: %s needs two integer arguments, ie: %s -10 5\n", option, option);
					return 1;
				}
				g_FromChunkX = atoi(NEXTARG);
				g_FromChunkZ = atoi(NEXTARG);
			} else if (strcmp(option, "-to") == 0) {
				if (!MOREARGS(2) || !isNumeric(POLLARG(1)) || !isNumeric(POLLARG(2))) {
					printf("Error: %s needs two integer arguments, ie: %s -5 20\n", option, option);
					return 1;
				}
				g_ToChunkX = atoi(NEXTARG)+1;
				g_ToChunkZ = atoi(NEXTARG)+1;
			} else if (strcmp(option, "-night") == 0) {
				g_Nightmode = true;
			} else if (strcmp(option, "-cave") == 0 || strcmp(option, "-underground") == 0) {
				g_Underground = true;
			} else if (strcmp(option, "-blendcave") == 0 || strcmp(option, "-blendcaves") == 0) {
				g_BlendUnderground = true;
			} else if (strcmp(option, "-skylight") == 0) {
				g_Skylight = true;
			} else if (strcmp(option, "-png") == 0) {
#ifdef WITHPNG
				gPng = true;
#else
				printf("mcmap was not compiled with libpng support.\n");
				return 1;
#endif
			} else if (strcmp(option, "-noise") == 0 || strcmp(option, "-dither") == 0) {
				if (!MOREARGS(1) || !isNumeric(POLLARG(1))) {
					printf("Error: %s needs an integer argument, ie: %s 10\n", option, option);
					return 1;
				}
				g_Noise = atoi(NEXTARG);
			} else if (strcmp(option, "-height") == 0) {
				if (!MOREARGS(1) || !isNumeric(POLLARG(1))) {
					printf("Error: %s needs an integer argument, ie: %s 100\n", option, option);
					return 1;
				}
				g_MapsizeY = atoi(NEXTARG);
			} else if (strcmp(option, "-mem") == 0) {
				if (!MOREARGS(1) || !isNumeric(POLLARG(1)) || atoi(POLLARG(1)) <= 0) {
					printf("Error: %s needs a positive integer argument, ie: %s 1000\n", option, option);
					return 1;
				}
				memlimitSet = true;
				memlimit = size_t(atoi(NEXTARG)) * size_t(1024 * 1024);
			} else if (strcmp(option, "-file") == 0) {
				if (!MOREARGS(1)) {
					printf("Error: %s needs one argument, ie: %s myworld.bmp\n", option, option);
					return 1;
				}
				outfile = NEXTARG;
			} else if (strcmp(option, "-colors") == 0) {
				if (!MOREARGS(1)) {
					printf("Error: %s needs one argument, ie: %s colors.txt\n", option, option);
					return 1;
				}
				colorfile = NEXTARG;
			} else if (strcmp(option, "-dumpcolors") == 0) {
				loadColors();
				if (!dumpColorsToFile("defaultcolors.txt")) {
					printf("Could not dump colors to defaultcolors.txt, error opening file.\n");
					return 1;
				}
				printf("Colors written to defaultcolors.txt\n");
				return 0;
			} else if (strcmp(option, "-north") == 0) {
				g_Orientation = North;
			} else if (strcmp(option, "-south") == 0) {
				g_Orientation = South;
			} else if (strcmp(option, "-east") == 0) {
				g_Orientation = East;
			} else if (strcmp(option, "-west") == 0) {
				g_Orientation = West;
			} else if (strcmp(option, "-help") == 0 || strcmp(option, "-h") == 0 || strcmp(option, "-?") == 0) {
				printHelp(argv[0]);
				return 0;
			} else {
				filename = (char*)option;
			}
		}
		wholeworld = (g_FromChunkX == UNDEFINED || g_ToChunkX == UNDEFINED);
	}
	// ########## end of command line parsing ##########

	if (filename == NULL) {
		printf("Error: No world given. Please add the path to your world to the command line.\n");
		return 1;
	}
	if (wholeworld && !scanWorldDirectory(filename)) {
		printf("Error accessing terrain at '%s'\n", filename);
		return 1;
	}
	if (g_MapsizeY < 1 || g_ToChunkX <= g_FromChunkX || g_ToChunkZ <= g_FromChunkZ) {
		printf("What to doooo, yeah, what to doooo... (English: max height < 1 or X/Z-width <= 0) %d %d %d\n", (int)g_MapsizeY, (int)g_MapsizeX, (int)g_MapsizeZ);
		return 1;
	}
	if (g_MapsizeY > CHUNKSIZE_Y) g_MapsizeY = CHUNKSIZE_Y;
	// Whole area to be rendered, in chunks
	// If -mem is omitted or high enough, this won't be needed
	gTotalFromChunkX = g_FromChunkX;
	gTotalFromChunkZ = g_FromChunkZ;
	gTotalToChunkX = g_ToChunkX;
	gTotalToChunkZ = g_ToChunkZ;
	// Don't allow ridiculously small values for big maps
	if (memlimit && memlimit < 200000000 && memlimit < size_t(g_MapsizeX * g_MapsizeZ * 150000)) {
		printf("Need at least %d MiB of RAM to render a map of that size.\n", int(float(g_MapsizeX) * g_MapsizeZ * .15f + 1));
		return 1;
	}

	// This decides whether a bmp or png is created
	assignFunctionPointers();

	// Mem check
	int bitmapX, bitmapY;
	size_t bitmapBytes = (*calcImageSize)(g_ToChunkX - g_FromChunkX, g_ToChunkZ - g_FromChunkZ, g_MapsizeY, bitmapX, bitmapY, false);
	// Cropping
	int cropLeft = 0, cropRight = 0, cropTop = 0, cropBottom = 0;
	if (wholeworld) {
		calcBitmapOverdraw(cropLeft, cropRight, cropTop, cropBottom);
		bitmapX -= (cropLeft + cropRight);
		bitmapY -= (cropTop + cropBottom);
		if (gPng) {
			bitmapBytes = size_t(bitmapX) * 4 * size_t(bitmapY);
		} else {
			bitmapBytes = (size_t(bitmapX * 3 + 3) & ~size_t(3)) * bitmapY;
		}
	}
	bool splitImage = false;
	int numSplitsX = 0;
	int numSplitsZ = 0;
	if (memlimit && memlimit < bitmapBytes + calcTerrainSize(g_ToChunkX - g_FromChunkX, g_ToChunkZ - g_FromChunkZ)) {
		// If we'd need more mem than allowed, we have to render groups of chunks...
		if (memlimit < bitmapBytes + 220 * size_t(1024 * 1024)) {
			// Warn about using incremental rendering if user didn't set limit manually
			if (!memlimitSet) {
				printf(" ***** PLEASE NOTE *****\n"
						"mcmap is using disk cached rendering as it has a default memory limit\n"
						"of 1800MiB. If you want to use more memory to render (=faster) use\n"
						"the -mem switch followed by the amount of memory in MiB to use.\n"
						"Start mcmap without any arguments to get more help.\n");
			} else {
				printf("Choosing disk caching strategy...\n");
			}
			// ...or even use disk caching
			splitImage = true;
		}
		// Split up map more and more, until the mem requirements are satisfied
		for (numSplitsX = 1, numSplitsZ = 2;;) {
			int subAreaX = ((gTotalToChunkX - gTotalFromChunkX) + (numSplitsX - 1)) / numSplitsX;
			int subAreaZ = ((gTotalToChunkZ - gTotalFromChunkZ) + (numSplitsZ - 1)) / numSplitsZ;
			int subBitmapX, subBitmapY;
			if (splitImage && (*calcImageSize)(subAreaX, subAreaZ, g_MapsizeY, subBitmapX, subBitmapY, true) + calcTerrainSize(subAreaX, subAreaZ) <= memlimit) {
				break; // Found a suitable partitioning
			} else if (!splitImage && bitmapBytes + calcTerrainSize(subAreaX, subAreaZ) <= memlimit) {
				break; // Found a suitable partitioning
			}
			//
			if (numSplitsZ > numSplitsX) {
				++numSplitsX;
			} else {
				++numSplitsZ;
			}
		}
	}

	// Always same random seed, as this is only used for block noise, which should give the same result for the same input every time
	srand(1337);
	// Load colormap from file
	loadColors(); // first load internal list, overwrite specific colors from file later if desired
	if (colorfile != NULL && fileExists(colorfile)) {
		if (!loadColorsFromFile(colorfile)) {
			printf("Error loading colors from %s: Opening failed.\n", colorfile);
			return 1;
		}
	} else if (colorfile != NULL) {
		printf("Error loading colors from %s: File not found.\n", colorfile);
		return 1;
	}

	if (outfile == NULL) {
		if (gPng) {
			outfile = (char*)"output.png";
		} else {
			outfile = (char*)"output.bmp";
		}
	}

	// open output file
	FILE *fileHandle = fopen(outfile, (splitImage ? "w+b" : "wb"));

	if (fileHandle == NULL) {
		printf("Error opening '%s' for writing.\n", outfile);
		return 1;
	}

	// This writes out the bitmap header and pre-allocates space if disk caching is used
	if (!(*createImage)(fileHandle, bitmapX, bitmapY, splitImage)) {
		printf("Error allocating bitmap. Check if you have enough free disk space.\n");
		return 1;
	}

	// Now here's the loop rendering all the required parts of the image.
	// All the vars previously used to define bounds will be set on each loop,
	// to create something like a virtual window inside the map.
	for (;;) {

		int bitmapStartX = 3, bitmapStartY = 5;
		if (numSplitsX) { // virtual window is set here
			// Set current chunk bounds according to number of splits. returns true if we're done
			if (prepareNextArea(numSplitsX, numSplitsZ, bitmapStartX, bitmapStartY)) {
				break;
			}
			// if image is split up, prepare memory block for next part
			if (splitImage) {
				bitmapStartX += 2;
				const int sizex = (g_ToChunkX - g_FromChunkX) * CHUNKSIZE_X * 2 + (g_ToChunkZ - g_FromChunkZ) * CHUNKSIZE_Z * 2;
				const int sizey = (int)g_MapsizeY * 2 + (g_ToChunkX - g_FromChunkX) * CHUNKSIZE_X + (g_ToChunkZ - g_FromChunkZ) * CHUNKSIZE_Z + 3;
				if (!(*loadImagePart)(fileHandle, bitmapStartX - cropLeft, bitmapStartY - cropTop, sizex, sizey)) {
					printf("Error loading partial image to render to.\n");
					return 1;
				}
			}
		}

		// More chunks are needed at the sides to get light and edge detection right at the edges
		// This makes code below a bit messy, as most of the time the surrounding chunks are ignored
		// By starting loops at CHUNKSIZE_X instead of 0.
		++g_ToChunkX;
		++g_ToChunkZ;
		--g_FromChunkX;
		--g_FromChunkZ;

		// For rotation, X and Z have to be swapped (East and West)
		if (g_Orientation == North || g_Orientation == South) {
			g_MapsizeZ = (g_ToChunkZ - g_FromChunkZ) * CHUNKSIZE_Z;
			g_MapsizeX = (g_ToChunkX - g_FromChunkX) * CHUNKSIZE_X;
		} else {
			g_MapsizeX = (g_ToChunkZ - g_FromChunkZ) * CHUNKSIZE_Z;
			g_MapsizeZ = (g_ToChunkX - g_FromChunkX) * CHUNKSIZE_X;
		}

		// Load world or part of world
		if (numSplitsX == 0 && wholeworld && !loadEntireTerrain()) {
			printf("Error loading terrain from '%s'\n", filename);
			return 1;
		} else if (numSplitsX != 0 || !wholeworld) {
			if (!loadTerrain(filename)) {
				printf("Error loading terrain from '%s'\n", filename);
				return 1;
			}
		}

		// If underground mode, remove blocks that don't seem to belong to caves
		if (g_Underground) {
			undergroundMode(false);
		}

		optimizeTerrain();

		// Finally, render terrain to file
		printf("Drawing map...\n");
		for (size_t x = CHUNKSIZE_X; x < g_MapsizeX - CHUNKSIZE_X; ++x) {
			printProgress(x - CHUNKSIZE_X, g_MapsizeX);
			for (size_t z = CHUNKSIZE_Z; z < g_MapsizeZ - CHUNKSIZE_Z; ++z) {
				const int bmpPosX = int((g_MapsizeZ - z - CHUNKSIZE_Z) * 2 + (x - CHUNKSIZE_X) * 2 + (splitImage ? -2 : bitmapStartX - cropLeft));
				int bmpPosY = int(g_MapsizeY * 2 + z + x - CHUNKSIZE_Z - CHUNKSIZE_X + (splitImage ? 0 : bitmapStartY - cropTop)) + 2;
				for (size_t y = 0; y < g_MapsizeY; ++y) {
					bmpPosY -= 2;
					uint8_t &c = BLOCKAT(x,y,z);
					if (c == AIR) continue;
					//float col = float(y) * .78f - 91;
					float brightnessAdjustment = (100.0f/(1.0f+exp(-(1.3f * float(y) / 16.0f)+6.0f))) - 91; // thx Donkey Kong
					if (g_BlendUnderground) brightnessAdjustment -= 168;
					// we use light if...
					if (g_Nightmode // nightmode is active, or
							|| (g_Skylight // skylight is used and
									&& (!BLOCK_AT_MAPEDGE(x, z)) // block is not edge of map (or if it is, has non-opaque block above)
											)) {
						int l = GETLIGHTAT(x, y, z); // find out how much light hits that block
						if (l == 0 && y+1 == g_MapsizeY) l = (g_Nightmode ? 3 : 15); // quickfix: assume maximum strength at highest level
						bool blocked[5] = {false, false, false, false, false}; // if light is blocked in one direction
						for (int i = 1; i < 4 && l <= 0; ++i) {
							// Need to make this a loop to deal with half-steps, fences, flowers and other special blocks
							blocked[0] |= (colors[BLOCKAT(x+i, y, z)][ALPHA] == 255);
							blocked[1] |= (colors[BLOCKAT(x, y, z+i)][ALPHA] == 255);
							blocked[2] |= (y+i >= g_MapsizeY || colors[BLOCKAT(x, y+i, z)][ALPHA] == 255);
							blocked[3] |= (colors[BLOCKAT(x+i, y+i, z)][ALPHA] == 255);
							blocked[4] |= (colors[BLOCKAT(x, y+i, z+i)][ALPHA] == 255);
							if (l <= 0 // if block is still dark and there are no translucent blocks around, stop
									&& blocked[0] && blocked[1] && blocked[2] && blocked[3] && blocked[4]) break;
							//
							if (!blocked[2] && l <= 0 && y+i < g_MapsizeY) l = GETLIGHTAT(x, y+i, z);
							if (!blocked[0] && l <= 0) l = GETLIGHTAT(x+i, y, z) - i/2;
							if (!blocked[1] && l <= 0) l = GETLIGHTAT(x, y, z+i) - i/2;
							if (!blocked[3] && l <= 0 && y+i < g_MapsizeY) l = (int)GETLIGHTAT(x+i, y+i, z) - i/2;
							if (!blocked[4] && l <= 0 && y+i < g_MapsizeY) l = (int)GETLIGHTAT(x, y+i, z+i) - i/2;
							//if (!blocked[2] && l <= 0 && y+i < g_MapsizeY) l = GETLIGHTAT(x+i/2, y+i/2, z+i/2) - i/2;
						}
						if (l < 0) l = 0;
						if (!g_Skylight) { // Night
							brightnessAdjustment -= (125 - l * 9);
						} else { // Day
							brightnessAdjustment -= (210 - l * 14);
						}
					}
					// Edge detection (this means where terrain goes 'down' and the side of the block is not visible)
					if ((y && y+1 < g_MapsizeY) // In bounds?
						&& BLOCKAT(x,y+1,z) == AIR // Only if block above is air
						&& (BLOCKAT(x-1,y-1,z-1) == c || BLOCKAT(x-1,y-1,z-1) == AIR) // block behind (from pov) this one is same type or air
						&& (BLOCKAT(x-1,y,z) == AIR || BLOCKAT(x,y,z-1) == AIR)) { // block TL/TR from this one is air = edge
							brightnessAdjustment += 12;
					}
					setPixel(bmpPosX, bmpPosY, c, brightnessAdjustment);
				}
			}
		}
		printProgress(10, 10);
		// Bitmap creation complete
		// unless we use....
		// Underground overlay mode
		if (g_BlendUnderground && !g_Underground) {
			// Load map data again, since block culling removed most of the blocks
			if (numSplitsX == 0 && wholeworld && !loadEntireTerrain()) {
				printf("Error loading terrain from '%s'\n", filename);
				return 1;
			} else if (numSplitsX != 0 || !wholeworld) {
				if (!loadTerrain(filename)) {
					printf("Error loading terrain from '%s'\n", filename);
					return 1;
				}
			}
			undergroundMode(true);
			optimizeTerrain();
			printf("Creating cave overlay...\n");
			for (size_t x = CHUNKSIZE_X; x < g_MapsizeX - CHUNKSIZE_X; ++x) {
				printProgress(x - CHUNKSIZE_X, g_MapsizeX);
				for (size_t z = CHUNKSIZE_Z; z < g_MapsizeZ - CHUNKSIZE_Z; ++z) {
					const size_t bmpPosX = (g_MapsizeZ - z - CHUNKSIZE_Z) * 2 + (x - CHUNKSIZE_X) * 2 + (splitImage ? -2 : bitmapStartX) - cropLeft;
					size_t bmpPosY = g_MapsizeY * 2 + z + x - CHUNKSIZE_Z - CHUNKSIZE_X + (splitImage ? 0 : bitmapStartY) - cropTop;
					for (size_t y = 0; y < MIN(g_MapsizeY, 64); ++y) {
						uint8_t &c = BLOCKAT(x,y,z);
						if (c != AIR) { // If block is not air (colors[c][3] != 0)
							(*blendPixel)(bmpPosX, bmpPosY, c, float(y + 30) * .0048f);
						}
						bmpPosY -= 2;
					}
				}
			}
			printProgress(10, 10);
		} // End blend-underground
		// If disk caching is used, save part to disk
		if (splitImage && !(*saveImagePart)(fileHandle)) {
			printf("Error saving partially rendered image.\n");
			return 1;
		}
		// No incremental rendering at all, so quit the loop
		if (numSplitsX == 0) break;
	}
	if (!splitImage) {
		printf("Writing to file...\n");
		(*saveImage)(fileHandle);
	} else if (gPng) {
#ifdef WITHPNG
		composeFinalImagePng();
#endif
	}
	fclose(fileHandle);

	printf("Job complete.\n");
	return 0;
}

void optimizeTerrain()
{
	// Remove invisible blocks from map (covered by other blocks from isometric pov)
	// Do so by "raytracing" every block from front to back..
	printf("Optimizing terrain...\n");
	size_t removed = 0;
	printProgress(0, 10);
	const size_t top = MIN(g_MapsizeY, 100) - 1;  // Some cheating here, as in most cases there is little to nothing up that high, and the few things that are won't slow down rendering too much
	const size_t progressMax = g_MapsizeX + g_MapsizeZ - 1 - CHUNKSIZE_Z;
	for (size_t x = CHUNKSIZE_X+1; x < g_MapsizeX - CHUNKSIZE_X; ++x) {
		for (size_t z = CHUNKSIZE_Z+1; z < g_MapsizeZ - CHUNKSIZE_Z; ++z) {
			blockCulling(x, top, z, removed);
		}
		for (size_t y = top; y > 0; --y) {
			blockCulling(x, y, g_MapsizeZ-1-CHUNKSIZE_Z, removed);
		}
		printProgress(x, progressMax);
	}
	for (size_t z = CHUNKSIZE_Z+1; z < g_MapsizeZ-1 - CHUNKSIZE_Z; ++z) {
		for (size_t y = top; y > 0; --y) {
			blockCulling(g_MapsizeX-1-CHUNKSIZE_X, y, z, removed);
		}
		printProgress(z + g_MapsizeX, progressMax);
	}
	printProgress(10, 10);
	printf("Removed %lu blocks\n", (unsigned long)removed);
}

inline void blockCulling(const size_t x, const size_t y, const size_t z, size_t &removed)
{	// Actually I just used 'removed' for debugging, but removing it
	// gives no speed increase at all, so why bother?
	bool cull = false; // Culling active?
	for (size_t i = 0; i < g_MapsizeY; ++i) {
		if (x < i || y < i || z < i) break;
		uint8_t &c = BLOCKAT(x-i, y-i, z-i);
		if (cull && c != AIR) {
			c = AIR;
			++removed;
		} else if (colors[c][ALPHA] == 255) {
			cull = true;
		}
	}
}

void undergroundMode(bool explore)
{	// This wipes out all blocks that are not caves/tunnels
	//int cnt[256];
	//memset(cnt, 0, sizeof(cnt));
	printf("Exploring underground...\n");
	if (explore) {
		clearLightmap();
		for (size_t x = CHUNKSIZE_X; x < g_MapsizeX-CHUNKSIZE_X; ++x) {
			printProgress(x - CHUNKSIZE_X, g_MapsizeX);
			for (size_t z = CHUNKSIZE_Z; z < g_MapsizeZ-CHUNKSIZE_Z; ++z) {
				for (size_t y = 0; y < MIN(g_MapsizeY, 64)-1; y++) {
					if (BLOCKAT(x,y,z) == TORCH) {
						// Torch
						BLOCKAT(x,y,z) = AIR;
						for (int ty = int(y) - 9; ty < int(y) + 9; ty+=2) { // The trick here is to only take into account
							if (ty < 0) continue; // areas around torches.
							if (ty >= int(g_MapsizeY)-1) break;
							for (int tz = int(z) - 18; tz < int(z) + 18; ++tz) {
								if (tz < CHUNKSIZE_Z) continue;
								if (tz >= int(g_MapsizeZ)-CHUNKSIZE_Z) break;
								for (int tx = int(x) - 18; tx < int(x) + 18; ++tx) {
									if (tx < CHUNKSIZE_X) continue;
									if (tx >= int(g_MapsizeX)-CHUNKSIZE_X) break;
									SETLIGHTNORTH(tx, ty, tz) = 0xFF;
								}
							}
						}
						// /
					}
				}
			}
		}
	}
	for (size_t x = 0; x < g_MapsizeX; ++x) {
		printProgress(x + g_MapsizeX * (explore ? 1 : 0), g_MapsizeX * (explore ? 2 : 1));
		for (size_t z = 0; z < g_MapsizeZ; ++z) {
			size_t ground = 0;
			size_t cave = 0;
			for (size_t y = g_MapsizeY-1; y < g_MapsizeY; --y) {
				uint8_t &c = BLOCKAT(x,y,z);
				if (c != AIR && cave > 0) { // Found a cave, leave floor
					if (c == GRASS || c == LEAVES || c == SNOW || GETLIGHTAT(x,y,z) == 0) {
						c = AIR; // But never count snow or leaves
					} //else cnt[*c]++;
					if (c != WATER && c != STAT_WATER) --cave;
				} else if (c != AIR) { // Block is not air, count up "ground"
					c = AIR;
					if (c != LOG && c != LEAVES && c != SNOW && c != WOOD && c != WATER && c != STAT_WATER) {
						++ground;
					}
				} else if (ground < 3) { // Block is air, if there was not enough ground above, don't trat that as a cave
					ground = 0;
				} else { // Thats a cave, draw next two blocks below it
					cave = 2;
				}
			}
		}
	}
	printProgress(10, 10);
	//for (int i = 0; i < 256; ++i) {
	//	if (cnt[i] == 0) continue;
	//	printf("Block %d: %d\n", i, cnt[i]);
	//}
}

bool prepareNextArea(int splitX, int splitZ, int &bitmapStartX, int &bitmapStartY)
{
	static int currentAreaX = -1;
	static int currentAreaZ = 0;
	// move on to next part and stop if we're done
	++currentAreaX;
	if (currentAreaX >= splitX) {
		currentAreaX = 0;
		++currentAreaZ;
	}
	if (currentAreaZ >= splitZ) {
		return true;
	}
	// For bright map edges
	if (g_Orientation == West || g_Orientation == East) {
		gAtBottomRight = (currentAreaZ + 1 == splitZ);
		gAtBottomLeft = (currentAreaX + 1 == splitX);
	} else {
		gAtBottomLeft = (currentAreaZ + 1 == splitZ);
		gAtBottomRight = (currentAreaX + 1 == splitX);
	}
	// Calc size of area to be rendered (in chunks)
	const int subAreaX = ((gTotalToChunkX - gTotalFromChunkX) + (splitX - 1)) / splitX;
	const int subAreaZ = ((gTotalToChunkZ - gTotalFromChunkZ) + (splitZ - 1)) / splitZ;
	// Adjust values for current frame. order depends on map orientation
	g_FromChunkX = gTotalFromChunkX + subAreaX * (g_Orientation == North || g_Orientation == West ? currentAreaX : splitX - (currentAreaX + 1));
	g_FromChunkZ = gTotalFromChunkZ + subAreaZ * (g_Orientation == North || g_Orientation == East ? currentAreaZ : splitZ - (currentAreaZ + 1));
	g_ToChunkX = g_FromChunkX + subAreaX;
	g_ToChunkZ = g_FromChunkZ + subAreaZ;
	// Bounds checking
	if (g_ToChunkX > gTotalToChunkX) g_ToChunkX = gTotalToChunkX;
	if (g_ToChunkZ > gTotalToChunkZ) g_ToChunkZ = gTotalToChunkZ;
	printf("Pass %d of %d...\n", int(currentAreaX + (currentAreaZ * splitX) + 1), int(splitX * splitZ));
	// Calulate pixel offsets in bitmap. Forgot how this works right after writing it, really.
	if (g_Orientation == North) {
		bitmapStartX = (((gTotalToChunkZ - gTotalFromChunkZ) * CHUNKSIZE_Z) * 2 + 3) // Center of image..
				- ((g_ToChunkZ - gTotalFromChunkZ) * CHUNKSIZE_Z * 2) // increasing Z pos will move left in bitmap
				+ ((g_FromChunkX - gTotalFromChunkX) * CHUNKSIZE_X * 2); // increasing X pos will move right in bitmap
		bitmapStartY = 5 + (g_FromChunkZ - gTotalFromChunkZ) * CHUNKSIZE_Z + (g_FromChunkX - gTotalFromChunkX) * CHUNKSIZE_X;
	} else if (g_Orientation == South) {
		const int tox = gTotalToChunkX - g_FromChunkX + gTotalFromChunkX;
		const int toz = gTotalToChunkZ - g_FromChunkZ + gTotalFromChunkZ;
		const int fromx = tox - (g_ToChunkX - g_FromChunkX);
		const int fromz = toz - (g_ToChunkZ - g_FromChunkZ);
		bitmapStartX = (((gTotalToChunkZ - gTotalFromChunkZ) * CHUNKSIZE_Z) * 2 + 3) // Center of image..
				- ((toz - gTotalFromChunkZ) * CHUNKSIZE_Z * 2) // increasing Z pos will move left in bitmap
				+ ((fromx - gTotalFromChunkX) * CHUNKSIZE_X * 2); // increasing X pos will move right in bitmap
		bitmapStartY = 5 + (fromz - gTotalFromChunkZ) * CHUNKSIZE_Z + (fromx - gTotalFromChunkX) * CHUNKSIZE_X;
	} else if (g_Orientation == East) {
		const int tox = gTotalToChunkX - g_FromChunkX + gTotalFromChunkX;
		const int fromx = tox - (g_ToChunkX - g_FromChunkX);
		bitmapStartX = (((gTotalToChunkX - gTotalFromChunkX) * CHUNKSIZE_X) * 2 + 3) // Center of image..
				- ((tox - gTotalFromChunkX) * CHUNKSIZE_X * 2) // increasing Z pos will move left in bitmap
				+ ((g_FromChunkZ - gTotalFromChunkZ) * CHUNKSIZE_Z * 2); // increasing X pos will move right in bitmap
		bitmapStartY = 5 + (fromx - gTotalFromChunkX) * CHUNKSIZE_X + (g_FromChunkZ - gTotalFromChunkZ) * CHUNKSIZE_Z;
	} else {
		const int toz = gTotalToChunkZ - g_FromChunkZ + gTotalFromChunkZ;
		const int fromz = toz - (g_ToChunkZ - g_FromChunkZ);
		bitmapStartX = (((gTotalToChunkX - gTotalFromChunkX) * CHUNKSIZE_X) * 2 + 3) // Center of image..
				- ((g_ToChunkX - gTotalFromChunkX) * CHUNKSIZE_X * 2) // increasing Z pos will move left in bitmap
				+ ((fromz - gTotalFromChunkZ) * CHUNKSIZE_Z * 2); // increasing X pos will move right in bitmap
		bitmapStartY = 5 + (g_FromChunkX - gTotalFromChunkX) * CHUNKSIZE_X + (fromz - gTotalFromChunkZ) * CHUNKSIZE_Z;
	}
	return false; // not done yet, return false
}

void assignFunctionPointers()
{
	if (gPng) {
#ifdef WITHPNG
		createImage = &createImagePng;
		saveImage = &saveImagePng;
		loadImagePart = &loadImagePartPng;
		setPixel = &setPixelPng;
		blendPixel = &blendPixelPng;
		saveImagePart = &saveImagePartPng;
		calcImageSize = &calcImageSizePng;
#endif
	} else {
		createImage = &createImageBmp;
		saveImage = &saveImageBmp;
		loadImagePart = &loadImagePartBmp;
		setPixel = &setPixelBmp;
		blendPixel = &blendPixelBmp;
		saveImagePart = &saveImagePartBmp;
		calcImageSize = &calcImageSizeBmp;
	}
}

void printHelp(char* binary)
{
	printf(
			////////////////////////////////////////////////////////////////////////////////
			"\nmcmap - an isometric minecraft alpha map rendering tool. Version " VERSION "\n\n"
			"Usage: %s [-from X Z -to X Z] [-night] [-cave] [-noise VAL] [...] WORLDPATH\n\n"
			"  -from X Z     sets the coordinate of the chunk to start rendering at\n"
			"  -to X Z       sets the coordinate of the chunk to end rendering at\n"
			"                Note: Currently you need both -from and -to to define\n"
			"                bounds, otherwise the entire world will be rendered.\n"
			"  -cave         renders a map of all caves that have been explored by players\n"
			"  -blendcave    overlay caves over normal map; doesn't work with incremental\n"
			"                rendering (some parts will be hidden)\n"
			"  -night        renders the world at night using blocklight (torches)\n"
			"  -skylight     use skylight when rendering map (shadows below trees etc.)\n"
			"                hint: using this with -night makes a difference\n"
			"  -noise VAL    adds some noise to certain blocks, reasonable values are 0-20\n"
			"  -height VAL   maximum height at which blocks will be rendered (1-128)\n"
			"  -file NAME    sets the output filename to 'NAME'; default is output.bmp\n"
			"  -mem VAL      sets the amount of memory (in MiB) used for rendering. mcmap\n"
			"                will use incremental rendering or disk caching to stick to\n"
			"                this limit. Default is 1800.\n"
			"  -colors NAME  loads user defined colors from file 'NAME'\n"
			"  -dumpcolors   creates a file which contains the default colors being used\n"
			"                for rendering. Can be used to modify them and then use -colors\n"
			"  -north -east -south -west\n"
			"                controls which direction will point to the *top left* corner\n"
			"                it only makes sense to pass one of them; East is default\n"
#ifdef WITHPNG
			"  -png          set output format to png instead of bmp\n"
#endif
			"\n    WORLDPATH is the path of the desired alpha world.\n\n"
			////////////////////////////////////////////////////////////////////////////////
			"Examples:\n\n"
#ifdef _WIN32
			"%s %%APPDATA%%\\.minecraft\\saves\\World1\n"
			"  - This would render your entire singleplayer world in slot 1\n"
			"%s -night -from -10 -10 -to 10 10 %%APPDATA%%\\.minecraft\\saves\\World1\n"
			"  - This would render the same world but at night, and only\n"
			"    from chunk (-10 -10) to chunk (10 10)\n"
#else
			"%s ~/.minecraft/saves/World1\n"
			"  - This would render your entire singleplayer world in slot 1\n"
			"%s -night -from -10 -10 -to 10 10 ~/.minecraft/saves/World1\n"
			"  - This would render the same world but at night, and only\n"
			"    from chunk (-10 -10) to chunk (10 10)\n"
#endif
			, binary, binary, binary);
}

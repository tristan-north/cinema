#include <fstream>
#include "loadtexture.h"

using namespace std;

// Example here http://www.mindcontrol.org/~hplus/graphics/dds-info/

GLuint loadTexture(const string filepath)
{
	union DDS_header {
	  struct {
		char            dwMagic[4];
		unsigned int    dwSize;
		unsigned int    dwFlags;
		unsigned int    dwHeight;
		unsigned int    dwWidth;
		unsigned int    dwPitchOrLinearSize;
		unsigned int    dwDepth;
		unsigned int    dwMipMapCount;
		unsigned int    dwReserved1[11];

		//  DDPIXELFORMAT
		struct {
		  unsigned int    dwSize;
		  unsigned int    dwFlags;
		  unsigned int    dwFourCC;
		  unsigned int    dwRGBBitCount;
		  unsigned int    dwRBitMask;
		  unsigned int    dwGBitMask;
		  unsigned int    dwBBitMask;
		  unsigned int    dwAlphaBitMask;
		}               sPixelFormat;

		//  DDCAPS2
		struct {
		  unsigned int    dwCaps1;
		  unsigned int    dwCaps2;
		  unsigned int    dwDDSX;
		  unsigned int    dwReserved;
		}               sCaps;
		unsigned int    dwReserved2;
	  };
	  char data[128];
	};

	ifstream file;
	file.open(filepath.c_str(), std::ifstream::binary);

	if( !file.is_open() ) {
		fprintf(stderr, "File %s can't be opened.", filepath.c_str());
		return 0;
	}

	printf("Loading: %s\n", filepath.c_str());

	// Read header
	DDS_header header;
	file.read((char*)&header, sizeof(header));

	// Verify the type of file
	if (strncmp(header.dwMagic, "DDS ", 4) != 0) {
		file.close();
		fprintf(stderr, "Failed to load dds. Incorrect format.");
		return 0;
	}

	// Print some info about the texture.
	/*
	printf("dwWidth: %d\ndwHeight: %d\n", header.dwWidth, header.dwHeight);
	printf("dwPitchOrLinearSize: %d\ndwMipMapCount: %d\n", header.dwPitchOrLinearSize, header.dwMipMapCount);
	printf("header.sPixelFormat.dwFourCC: %d\n", header.sPixelFormat.dwFourCC);
	printf("header.sPixelFormat.dwFlags: %#010x\n", header.sPixelFormat.dwFlags);
	printf("header.sPixelFormat.dwRGBBitCount: %d\n", header.sPixelFormat.dwRGBBitCount);
	*/

	size_t xSize = header.dwWidth;
	size_t ySize = header.dwHeight;
	size_t mipMapCount = header.dwMipMapCount;
	size_t bytesPerPixel = 4;
	size_t size = xSize * ySize * bytesPerPixel;

	GLuint textureId;
	glGenTextures(1, &textureId);
	glBindTexture(GL_TEXTURE_2D, textureId);

	unsigned char *data = (unsigned char*)malloc(size);

	// For each mipmap.
	for( size_t level = 0; level < mipMapCount; level++ ) {
		file.read((char*)data, size);

		glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA8, xSize, ySize, 0,
					 GL_BGRA, GL_UNSIGNED_BYTE, data);
		xSize /= 2;
		ySize /= 2;
		size = xSize * ySize * bytesPerPixel;
	}

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	free(data);
	file.close();

	return textureId;
}

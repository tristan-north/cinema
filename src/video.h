#ifndef VIDEO_H
#define VIDEO_H

#include <string>

int videoInitialize(const char *filepath);
// Copies frame pixel data into *pixels which needs to be
// videoGetWidth() * videoGetHeight() * 3 bytes big.
// Returns 0 if finished reading video, otherise 1.
int videoGetFramePixels(unsigned char *pixels);
int videoGetWidth();
int videoGetHeight();

#endif // VIDEO_H

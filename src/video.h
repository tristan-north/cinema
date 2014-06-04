#ifndef VIDEO_H
#define VIDEO_H

#define FF_REFRESH_EVENT (SDL_USEREVENT)

int video_initialize(const char *filepath);

unsigned char *video_get_frame_pixels();
void video_refresh_timer(void *userdata);

int video_get_width();
int video_get_height();
void video_shutdown();

#endif // VIDEO_H

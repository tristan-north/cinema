/*

  VIDEO STUFF:
   - Make it make use of the extern global running instead of quit.
   - Try putenv("SDL_AUDIODRIVER=DirectSound"); if having sound problems.
   - This page http://osdl.sourceforge.net/main/documentation/rendering/SDL-audio.html
	 says to call SDL_SetVideoMode (now SDL_CreateWindow) before calling SDL_OpenAudio).


 - If audio finishes playing, program won't close properly (some thread still running probably).
 - Use RGB matte (and A?) in screen lightmask maps.
 - Have a "Please press space to start video." Texture initially on the screen.
 - Check "Slow pixel transfer performance" here http://www.opengl.org/wiki/Common_Mistakes
 - If video finishes, update the screen texture with a black texture, otherwise it just holds on the last frame.
 - Try to reduce screen texture upload time (glTexSubImage2D) by doing it in smaller res chunks.




OpenGL tutorials:
http://www.arcsynthesis.org/gltut/
http://duriansoftware.com/joe/An-intro-to-modern-OpenGL.-Table-of-Contents.html
http://www.mbsoftworks.sk/index.php?page=tutorials&series=1&tutorial=6

Fast mipmap generation references:
http://stackoverflow.com/questions/19428304/gltexstorage2d-returns-out-of-memory
http://stackoverflow.com/questions/15405869/is-gltexstorage2d-imperative-when-auto-generating-mipmaps
http://www.opengl.org/wiki/Common_Mistakes#Automatic_mipmap_generation


ffmpeg tutorials:
http://dranger.com/ffmpeg/tutorial01.html
Using updated code from https://github.com/phamquy/FFmpeg-tutorial-samples
and the more up to date version https://github.com/chelyaev/ffmpeg-tutorial
and the resampling fix version https://github.com/illuusio/ffmpeg-tutorial

*/

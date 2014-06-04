/*


  - Check that the video displaying system works ok. Don't ignore when video_display is called.
  - Try and get the flash video file audio to work ok.
  - Instead of down sampling audio to 44100 Hz, try up sampling to 48000.
  - Account for different aspect ratio videos.
  - Support more than 2 audio channels. At the moment always resampling the audio to 2 channel.


 - Use RGB matte (and A?) in screen lightmask maps.
 - Have a "Please press space to start video." Texture initially on the screen.
 - If video finishes, update the screen texture with a black texture, otherwise it just holds on the last frame.
 - If audio finishes playing it repeats the last chunk, play silence instead.


OpenGL resources:
http://www.arcsynthesis.org/gltut/
http://duriansoftware.com/joe/An-intro-to-modern-OpenGL.-Table-of-Contents.html
http://www.mbsoftworks.sk/index.php?page=tutorials&series=1&tutorial=6


ffmpeg resources:
http://dranger.com/ffmpeg/tutorial01.html
Using updated code from https://github.com/phamquy/FFmpeg-tutorial-samples
and the more up to date version https://github.com/chelyaev/ffmpeg-tutorial
and the resampling fix version https://github.com/illuusio/ffmpeg-tutorial

*/

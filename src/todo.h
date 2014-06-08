/*


  - Try and get the flash video file audio to work ok.
  - Instead of down sampling audio to 44100 Hz, try up sampling to 48000.
  - Support more than 2 audio channels. At the moment always resampling the audio to 2 channel.


 - Use RGB matte (and A?) in screen lightmask maps.
 - Have a "Please press space to start video." Texture initially on the screen.
 - Try mipmapping the screen texture.
 - Figure out best way to do multisampling.
   There's a post about it here https://developer.oculusvr.com/forums/viewtopic.php?f=20&t=8680#p117684



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

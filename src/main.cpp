// GLFWOculusRiftTest
// (c) cThrough 2014 (Daniel Dekkers)
// Version 2014052302 Based on OculusSDK 3.0.2 Preview

#include <Windows.h>
#include <GL/glew.h>
#include <OVR.h>
#include <OVR_CAPI_GL.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <algorithm>

#include "utilities.h"
#include "objloader.h"
#include "video.h"

using namespace std;

const bool l_FullScreen = false;
const bool l_MultiSampling = false;

// Externs
bool running = true;
GLuint program = 0;
GLint p_matrix_ufm = 0;
GLint mv_matrix_ufm = 0;
GLuint texture_ufm = 0;
objRenderData room, screen;

bool pollEvent()
{
	SDL_Event event;
	while (SDL_PollEvent(&event))
	{
			switch (event.type) {
			case SDL_QUIT:
					return false;
			case SDL_KEYDOWN:
					switch (event.key.keysym.sym) {
					case SDLK_ESCAPE:
							return false;
					}
					break;
			}
	}

	return true;
}

int main(int argc, char *argv[])
{
	// Get path of video file.
	string videoFilePath;
	if( argc <= 1 ) {
		videoFilePath = pickVideo();
		if(videoFilePath == "") return 0;
	} else {
		videoFilePath = string(argv[1]);
	}

	// Open and validate video file.
	if( videoInitialize(videoFilePath.c_str()) < 0 )
		return -1;

	// Get path of assets dir.
	string assetsDir = argv[0];
	assetsDir.erase(assetsDir.find_last_of('\\')+1);

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);

	// Rift init.
	ovrHmd l_Hmd;
	ovrHmdDesc l_HmdDesc;
	ovrFovPort l_EyeFov[2];
	ovrGLConfig l_Cfg;
	ovrEyeRenderDesc l_EyeRenderDesc[2];

	ovr_Initialize();
	l_Hmd = ovrHmd_Create(0);
	if (!l_Hmd) l_Hmd = ovrHmd_CreateDebug(ovrHmd_DK1);
	ovrHmd_GetDesc(l_Hmd, &l_HmdDesc);
	ovrHmd_StartSensor(l_Hmd, ovrSensorCap_Orientation | ovrSensorCap_YawCorrection | ovrSensorCap_Position, ovrSensorCap_Orientation);
	printf("\n\n\n");

	// Window creation.
	int x = SDL_WINDOWPOS_CENTERED;
	int y = SDL_WINDOWPOS_CENTERED;
	Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;

	if (l_FullScreen == true) {
			x = l_HmdDesc.WindowsPos.x;
			y = l_HmdDesc.WindowsPos.y;
			windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	}

	ovrSizei l_ClientSize;
	l_ClientSize.w = l_HmdDesc.Resolution.w; // 1280 for DK1...
	l_ClientSize.h = l_HmdDesc.Resolution.h; // 800 for DK1...
	SDL_Window *window = SDL_CreateWindow("Oculus Rift SDL2 OpenGL Demo", x, y, l_ClientSize.w, l_ClientSize.h, windowFlags);

	SDL_GLContext context = SDL_GL_CreateContext(window);

	// Glew init.
	glewExperimental = GL_TRUE;
	GLenum l_Result = glewInit();
	if (l_Result!=GLEW_OK) {
		printf("glewInit() error.\n");
		exit(EXIT_FAILURE);
	}

	// We will do some offscreen rendering, setup FBO...
	ovrSizei l_TextureSizeLeft = ovrHmd_GetFovTextureSize(l_Hmd, ovrEye_Left, l_HmdDesc.DefaultEyeFov[0], 1.0f);
	ovrSizei l_TextureSizeRight = ovrHmd_GetFovTextureSize(l_Hmd, ovrEye_Right, l_HmdDesc.DefaultEyeFov[1], 1.0f);
	ovrSizei l_TextureSize;
	l_TextureSize.w = l_TextureSizeLeft.w + l_TextureSizeRight.w;
	l_TextureSize.h = (l_TextureSizeLeft.h>l_TextureSizeRight.h ? l_TextureSizeLeft.h : l_TextureSizeRight.h);

	// Create FBO...
	GLuint l_FBOId;
	glGenFramebuffers(1, &l_FBOId);
	glBindFramebuffer(GL_FRAMEBUFFER, l_FBOId);

	// The texture we're going to render to...
	GLuint l_TextureId;
	glGenTextures(1, &l_TextureId);
	// "Bind" the newly created texture : all future texture functions will modify this texture...
	glBindTexture(GL_TEXTURE_2D, l_TextureId);
	// Give an empty image to OpenGL (the last "0")
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, l_TextureSize.w, l_TextureSize.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	// Linear filtering...
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	// Create Depth Buffer...
	GLuint l_DepthBufferId;
	glGenRenderbuffers(1, &l_DepthBufferId);
	glBindRenderbuffer(GL_RENDERBUFFER, l_DepthBufferId);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, l_TextureSize.w, l_TextureSize.h);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, l_DepthBufferId);

	// Set the texture as our colour attachment #0...
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, l_TextureId, 0);

	// Set the list of draw buffers...
	GLenum l_GLDrawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
	glDrawBuffers(1, l_GLDrawBuffers); // "1" is the size of DrawBuffers

	// Check if everything is OK...
	GLenum l_Check = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
	if (l_Check!=GL_FRAMEBUFFER_COMPLETE)
	{
		printf("There is a problem with the FBO.\n");
		exit(EXIT_FAILURE);
	}

	// Unbind...
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	// Oculus Rift eye configurations...
	l_EyeFov[0] = l_HmdDesc.DefaultEyeFov[0];
	l_EyeFov[1] = l_HmdDesc.DefaultEyeFov[1];

	SDL_SysWMinfo info;
	SDL_VERSION(&info.version);
	SDL_GetWindowWMInfo(window, &info);

	l_Cfg.OGL.Header.API = ovrRenderAPI_OpenGL;
	l_Cfg.OGL.Header.Multisample = (l_MultiSampling ? 1 : 0);
	l_Cfg.OGL.Header.RTSize.w = l_ClientSize.w;
	l_Cfg.OGL.Header.RTSize.h = l_ClientSize.h;
	l_Cfg.OGL.Window = info.info.win.window;

	int l_DistortionCaps = ovrDistortionCap_Chromatic | ovrDistortionCap_TimeWarp;
	ovrHmd_ConfigureRendering(l_Hmd, &l_Cfg.Config, l_DistortionCaps, l_EyeFov, l_EyeRenderDesc);

	ovrGLTexture l_EyeTexture[2];
	l_EyeTexture[0].OGL.Header.API = ovrRenderAPI_OpenGL;
	l_EyeTexture[0].OGL.Header.TextureSize.w = l_TextureSize.w;
	l_EyeTexture[0].OGL.Header.TextureSize.h = l_TextureSize.h;
	l_EyeTexture[0].OGL.Header.RenderViewport.Pos.x = 0;
	l_EyeTexture[0].OGL.Header.RenderViewport.Pos.y = 0;
	l_EyeTexture[0].OGL.Header.RenderViewport.Size.w = l_TextureSize.w/2;
	l_EyeTexture[0].OGL.Header.RenderViewport.Size.h = l_TextureSize.h;
	l_EyeTexture[0].OGL.TexId = l_TextureId;

	// Right eye the same, except for the x-position in the texture...
	l_EyeTexture[1] = l_EyeTexture[0];
	l_EyeTexture[1].OGL.Header.RenderViewport.Pos.x = (l_TextureSize.w+1)/2;


	initializeGeo(assetsDir);
	initializeTextures(assetsDir, videoGetWidth(), videoGetHeight());

	GLuint program = initializeProgram();

	OVR::Matrix4f camPosition = OVR::Matrix4f::Translation(0.0f, -1.313f, -1.6f);

	// Render loop
	glDepthFunc(GL_LEQUAL);
	glEnable(GL_DEPTH_TEST);
	glClearColor(0.2f, 0.3f, 0.4f, 1.0f);
	while (running)
	{
		running = pollEvent();

		double startTime = ovr_GetTimeInSeconds(); UNREFERENCED_PARAMETER(startTime);

		ovrFrameTiming m_HmdFrameTiming = ovrHmd_BeginFrame(l_Hmd, 0); UNREFERENCED_PARAMETER(m_HmdFrameTiming);

		// Bind the FBO...
		glBindFramebuffer(GL_FRAMEBUFFER, l_FBOId);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		for (int l_EyeIndex=0; l_EyeIndex<ovrEye_Count; l_EyeIndex++)
		{
			ovrEyeType l_Eye = l_HmdDesc.EyeRenderOrder[l_EyeIndex];
			ovrPosef l_EyePose = ovrHmd_BeginEyeRender(l_Hmd, l_Eye);

			glViewport(l_EyeTexture[l_Eye].OGL.Header.RenderViewport.Pos.x,      // StartX
					   l_EyeTexture[l_Eye].OGL.Header.RenderViewport.Pos.y,      // StartY
					   l_EyeTexture[l_Eye].OGL.Header.RenderViewport.Size.w,     // Width
					   l_EyeTexture[l_Eye].OGL.Header.RenderViewport.Size.h);      // Height

			// Get Projection and ModelView matrici from the device...
			OVR::Matrix4f l_ProjectionMatrix = ovrMatrix4f_Projection(
						l_EyeRenderDesc[l_Eye].Fov, 0.1f, 100.0f, true);
			OVR::Quatf l_Orientation = OVR::Quatf(l_EyePose.Orientation);
			OVR::Matrix4f l_ModelViewMatrix = OVR::Matrix4f(l_Orientation.Inverted());

			OVR::Matrix4f ipdOffset = OVR::Matrix4f::Translation(l_EyeRenderDesc[l_Eye].ViewAdjust);

			// Setup rendering
			glUseProgram(program);
			glUniform1i(texture_ufm, 0);   // 0 is first texture unit.
			glActiveTexture(GL_TEXTURE0);  // Activates first texture unit

			glUniformMatrix4fv(p_matrix_ufm, 1, GL_TRUE, &l_ProjectionMatrix.M[0][0]);

			l_ModelViewMatrix = ipdOffset * l_ModelViewMatrix;
			l_ModelViewMatrix = l_ModelViewMatrix * camPosition;
			glUniformMatrix4fv(mv_matrix_ufm, 1, GL_TRUE, &l_ModelViewMatrix.M[0][0]);

			// Render room
			glBindVertexArray(room.vao);
			glBindTexture(GL_TEXTURE_2D, room.texture);
			glDrawArrays(GL_TRIANGLES, 0, room.numTriangles*3);

			// Get new frame of video and setup screen texture.
			glBindTexture(GL_TEXTURE_2D, screen.texture);
//			if(videoGetFramePixels(videoPixels)) {
//				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, videoGetWidth(), videoGetHeight(),
//								GL_RGB, GL_UNSIGNED_BYTE, videoPixels);
//			}

			// Render screen
			glBindVertexArray(screen.vao);
			glDrawArrays(GL_TRIANGLES, 0, screen.numTriangles*3);

			// Cleanup
			glBindVertexArray(0);
			glBindTexture(GL_TEXTURE_2D, 0);
			glUseProgram(0);

			ovrHmd_EndEyeRender(l_Hmd, l_Eye, l_EyePose, &l_EyeTexture[l_Eye].Texture);
		}
//		printf("%.2f ms\n", (ovr_GetTimeInSeconds() - startTime)*1000);

		// Unbind the FBO, back to normal drawing...
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		ovrHmd_EndFrame(l_Hmd);

		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // Unbind GL_ELEMENT_ARRAY_BUFFER for our own vertex arrays to work...
		glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind GL_ARRAY_BUFFER for our own vertex arrays to work...
		glUseProgram(0); // Oculus shader is still active, turn it off...

	}

	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);

	printf("\n\n");

	ovrHmd_Destroy(l_Hmd);
	ovr_Shutdown();

	SDL_Quit();
	exit(EXIT_SUCCESS);
}

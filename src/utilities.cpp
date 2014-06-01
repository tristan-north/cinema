#include "utilities.h"
#include "objloader.h"
#include "loadtexture.h"

#include <algorithm>
#include <Windows.h>

using namespace std;

extern GLuint program;
extern GLint p_matrix_ufm;
extern GLint mv_matrix_ufm;
extern GLuint texture_ufm;
extern objRenderData room, screen;

GLuint createShader(GLenum eShaderType, const string &strShaderFile)
{
	GLuint shader = glCreateShader(eShaderType);
	const char *strFileData = strShaderFile.c_str();
	glShaderSource(shader, 1, &strFileData, NULL);

	glCompileShader(shader);

	GLint status;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint infoLogLength;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);

		GLchar *strInfoLog = new GLchar[infoLogLength + 1];
		glGetShaderInfoLog(shader, infoLogLength, NULL, strInfoLog);

		const char *strShaderType = NULL;
		switch(eShaderType)
		{
		case GL_VERTEX_SHADER: strShaderType = "vertex"; break;
		case GL_GEOMETRY_SHADER: strShaderType = "geometry"; break;
		case GL_FRAGMENT_SHADER: strShaderType = "fragment"; break;
		}

		fprintf(stderr, "Compile failure in %s shader:\n%s\n", strShaderType, strInfoLog);
		delete[] strInfoLog;
	}

	return shader;
}

GLuint createProgram(const vector<GLuint> &shaderList)
{
	GLuint program = glCreateProgram();

	for(size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
		glAttachShader(program, shaderList[iLoop]);

	glLinkProgram(program);

	GLint status;
	glGetProgramiv (program, GL_LINK_STATUS, &status);
	if (status == GL_FALSE)
	{
		GLint infoLogLength;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);

		GLchar *strInfoLog = new GLchar[infoLogLength + 1];
		glGetProgramInfoLog(program, infoLogLength, NULL, strInfoLog);
		fprintf(stderr, "Linker failure: %s\n", strInfoLog);
		delete[] strInfoLog;
	}

	for(size_t iLoop = 0; iLoop < shaderList.size(); iLoop++)
		glDetachShader(program, shaderList[iLoop]);

	return program;
}

const string vertexShaderString(
	"#version 330\n"
	"uniform mat4 p_matrix, mv_matrix;"
	"layout(location = 0) in vec3 position;"
	"layout(location = 1) in vec3 normal;"
	"layout(location = 2) in vec2 uv;"
	"out vec3 vertexNormal;"
	"out vec2 vertexUV;"
	"void main(){"
		"vec4 eyePosition = mv_matrix * vec4(position, 1.0);"
		"gl_Position = p_matrix * eyePosition;"
		"vertexNormal = normal;"
		"vertexUV = uv;"
	"}"
);

const string fragmentShaderString(
	"#version 330\n"
	"uniform sampler2D texSampler;"
	"in vec3 vertexNormal;"
	"in vec2 vertexUV;"
	"out vec4 outputColor;"
	"void main(){"
		"outputColor = vec4(texture(texSampler, vertexUV).xyz, 1.0);"
	"}"
);

GLuint initializeProgram()
{
	vector<GLuint> shaderList;
	shaderList.push_back(createShader(GL_VERTEX_SHADER, vertexShaderString));
	shaderList.push_back(createShader(GL_FRAGMENT_SHADER, fragmentShaderString));
	GLuint program = createProgram(shaderList);
	for_each(shaderList.begin(), shaderList.end(), glDeleteShader);

	p_matrix_ufm = glGetUniformLocation(program, "p_matrix");
	mv_matrix_ufm = glGetUniformLocation(program, "mv_matrix");
	texture_ufm = glGetUniformLocation(program, "texSampler");

	return program;
}

void initializeGeo(string geoDir)
{
	// Load the room .obj file.
	GLfloat* verts;
	GLfloat* normals;
	GLfloat* uvs;

	room.numTriangles = objLoader(geoDir.append("testModel.obj"), &verts, &normals, &uvs);
	if(room.numTriangles==0) exit(EXIT_FAILURE);
	printf("Triangles: %ld\n", room.numTriangles);

	createVAO(room, verts, normals, uvs);
	delete[] verts;
	delete[] normals;
	delete[] uvs;

	// Create the screen geo.
	GLfloat screenVerts[] = {-2.4f, 2.658f, -2.412f,
							 -2.4f, 0.658f, -2.412f,
							  2.4f, 0.658f, -2.412f,

							 -2.4f, 2.658f, -2.412f,
							  2.4f, 0.658f, -2.412f,
							  2.4f, 2.658f, -2.412f};

	GLfloat screenNormals[] = {0.0f, 0.0f, 1.0f,
							   0.0f, 0.0f, 1.0f,
							   0.0f, 0.0f, 1.0f,

							   0.0f, 0.0f, 1.0f,
							   0.0f, 0.0f, 1.0f,
							   0.0f, 0.0f, 1.0f};

	GLfloat screenUvs[] = {0.0f, 0.0f,
						   0.0f, 1.0f,
						   1.0f, 1.0f,

						   0.0f, 0.0f,
						   1.0f, 1.0f,
						   1.0f, 0.0f};

	screen.numTriangles = 2;
	createVAO(screen, screenVerts, screenNormals, screenUvs);
}

void createVAO(objRenderData &renderData, GLfloat *verts, GLfloat *normals, GLfloat *uvs)
{
	// Create and bind a VAO (this stores all the VBO state).
	glGenVertexArrays(1, &renderData.vao);
	glBindVertexArray(renderData.vao);

	/**************************/
	// Create and bind a BO for vertex position
	GLuint posBuffer;
	glGenBuffers(1, &posBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, posBuffer);

	// copy position data into the buffer object
	glBufferData(GL_ARRAY_BUFFER, renderData.numTriangles*3*3*sizeof(float), verts, GL_STATIC_DRAW);

	// set up vertex attributes
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
	/**************************/
	// Create and bind a BO for vertex normal
	GLuint normalBuffer;
	glGenBuffers(1, &normalBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, normalBuffer);

	// copy data into the buffer object
	glBufferData(GL_ARRAY_BUFFER, renderData.numTriangles*3*3*sizeof(float), normals, GL_STATIC_DRAW);

	// set up vertex attributes
	glEnableVertexAttribArray(1);
	glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 0, 0);
	/**************************/
	// Create and bind a BO for vertex uv
	GLuint uvBuffer;
	glGenBuffers(1, &uvBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, uvBuffer);

	// copy data into the buffer object
	glBufferData(GL_ARRAY_BUFFER, renderData.numTriangles*3*2*sizeof(float), uvs, GL_STATIC_DRAW);

	// set up vertex attributes
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);
	/**************************/

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArray(0);
}

void initializeTextures(string texDir, size_t screenTexWidth, size_t screenTexHeight)
{
	room.texture = loadTexture(texDir.append("testTex.DDS"));

	// Setup screen texture.
	glGenTextures(1, &screen.texture);
	glBindTexture(GL_TEXTURE_2D, screen.texture);

	unsigned char *pixels = (unsigned char*)malloc(screenTexWidth*screenTexHeight*3);
	memset(pixels, 0x00, screenTexWidth*screenTexHeight*3);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, screenTexWidth, screenTexHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels);
//	glTexStorage2D(GL_TEXTURE_2D, 2, GL_RGB, res, res);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);  // Disable mipmapping.
	glBindTexture(GL_TEXTURE_2D, 0);

	free(pixels);
}

string pickVideo()
{
	OPENFILENAMEA ofn;       // common dialog box structure
	char szFile[260];       // buffer for file name

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.lpstrFile = szFile;
	// Set lpstrFile[0] to '\0' so that GetOpenFileName does not
	// use the contents of szFile to initialize itself.
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "Video File (*.mkv, *.avi, *.mp4, *.mpeg, *.wmv, *.flv)\0*"
			".MKV;*.AVI;*.MP4;*.MPEG;*.WMV;*.FLV\0All\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrInitialDir = NULL;
	ofn.lpstrTitle = "Select Video File";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	if (GetOpenFileNameA(&ofn) == TRUE)
		return string(ofn.lpstrFile);
	else
		return string("");
}



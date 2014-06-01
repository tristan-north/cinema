#ifndef UTILITIES_H
#define UTILITIES_H

#include <string>
#include <vector>
#include <GL/glew.h>

struct objRenderData {
	GLuint vao = 0;
	size_t numTriangles = 0;
	GLuint texture = 0;
};

GLuint createShader(GLenum eShaderType, const std::string &strShaderFile);
GLuint createProgram(const std::vector<GLuint> &shaderList);
GLuint initializeProgram();
void initializeGeo(std::string geoDir);
void createVAO(objRenderData &renderData, GLfloat *verts, GLfloat *normals, GLfloat *uvs);
void initializeTextures(std::string texDir, size_t screenTexWidth, size_t screenTexHeight);
std::string pickVideo();


#endif // UTILITIES_H

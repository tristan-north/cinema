#include <fstream>
#include <assert.h>
#include <algorithm>
#include <OVR.h>
#include <vector>
#include "objloader.h"
#include "utilities.h"

using namespace std;

typedef OVR::Vector3f Vector3;
typedef OVR::Vector2f Vector2;

size_t getNormalIndexFromString(const string &str);
size_t getUVIndexFromString(const string &str);
void computeNormal(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, Vector3 &normal);
vector<string> getNumbersAsStrings(const string& line);

struct Triangle {
	size_t v0, v1, v2;
	size_t n0, n1, n2;
	size_t u0, u1, u2;
};

size_t objLoader(const std::string filepath, float *verts[], float *normals[], float *uvs[]) {
	ifstream file;
	file.open(filepath.c_str());

	if( !file.is_open() ) {
		fprintf(stderr, "File %s can't be opened.", filepath.c_str());
		return 0;
	}

	printf("Loading: %s\n", filepath.c_str());

	size_t numTriangles = 0;
	string line;
	vector<Vector3> vertsTempList;
	vector<Vector3> normalsTempList;
	vector<Vector2> uvsTempList;
	vector<Triangle> trianglesTempList;
	vector<string> numbersAsStrings;
	while( file.good() ) {
		getline(file, line);
		if( line.length() < 4 )
			continue;

		// Process line defining vertex.
		if( line.at(0) == 'v' && line.at(1) == ' ') {
			numbersAsStrings = getNumbersAsStrings(line);
			assert(numbersAsStrings.size() == 3);

			float x = (float)atof(numbersAsStrings[0].c_str());
			float y = (float)atof(numbersAsStrings[1].c_str());
			float z = (float)atof(numbersAsStrings[2].c_str());

			vertsTempList.push_back(Vector3(x, y, z));
		}

		// Process line defining uv.
		else if( line.at(0) == 'v' && line.at(1) == 't') {
			numbersAsStrings = getNumbersAsStrings(line);
			assert(numbersAsStrings.size() == 2);

			float u = (float)atof(numbersAsStrings[0].c_str());
			float v = (float)atof(numbersAsStrings[1].c_str());

			uvsTempList.push_back(Vector2(u, v));
		}
		// Process line defining normal.
		else if( line.at(0) == 'v' && line.at(1) == 'n') {
			numbersAsStrings = getNumbersAsStrings(line);
			assert(numbersAsStrings.size() == 3);

			float x = (float)atof(numbersAsStrings[0].c_str());
			float y = (float)atof(numbersAsStrings[1].c_str());
			float z = (float)atof(numbersAsStrings[2].c_str());

			normalsTempList.push_back(Vector3(x, y, z));
		}
		// Process line defining face.
		else if( line.at(0) == 'f' ) {
			// Need to -1 from indicies in obj file as they start at 1 not 0.
			size_t vIndex0=0, vIndex1=0, vIndex2=0, nIndex0=0, nIndex1=0, nIndex2=0, uIndex0=0, uIndex1=0, uIndex2=0;
			numbersAsStrings = getNumbersAsStrings(line);
			assert(numbersAsStrings.size() > 2);

			// If face defined contains normal index.
			bool normalIsDefined = false;
			if( count(numbersAsStrings[0].begin(), numbersAsStrings[0].end(), '/') == 2) {
				normalIsDefined = true;
			}

			vIndex0 = atoi(numbersAsStrings[0].c_str())-1;
			if(normalIsDefined) nIndex0 = getNormalIndexFromString(numbersAsStrings[0])-1;
			uIndex0 = getUVIndexFromString(numbersAsStrings[0])-1;

			// Need to account for faces defined by more than 3 verts and convert
			// them to triangles. Number of triangles created is number of verts -2.
			for( size_t i = 2; i < numbersAsStrings.size(); i++ ) {
				vIndex1 = atoi(numbersAsStrings[i - 1].c_str())-1;
				vIndex2 = atoi(numbersAsStrings[i].c_str())-1;

				uIndex1 = getUVIndexFromString(numbersAsStrings[i - 1].c_str())-1;
				uIndex2 = getUVIndexFromString(numbersAsStrings[i].c_str())-1;

				if(normalIsDefined) {
					nIndex1 = getNormalIndexFromString(numbersAsStrings[i - 1])-1;
					nIndex2 = getNormalIndexFromString(numbersAsStrings[i])-1;
				}
				else {
					Vector3 normal;
					computeNormal(vertsTempList[vIndex0], vertsTempList[vIndex1], vertsTempList[vIndex2], normal);
					normalsTempList.push_back(normal);

					nIndex0 = nIndex1 = nIndex2 = normalsTempList.size()-1;
				}

				Triangle tri;
				tri.v0 = vIndex0; tri.v1 = vIndex1; tri.v2 = vIndex2;
				tri.n0 = nIndex0; tri.n1 = nIndex1; tri.n2 = nIndex2;
				tri.u0 = uIndex0; tri.u1 = uIndex1; tri.u2 = uIndex2;

				trianglesTempList.push_back(tri);

				numTriangles++;
			}
		}
		// Process line defining group name.
//        else if( line.at(0) == 'g' ) {
//            string groupName = line.substr(2);
//            materialToAssign = world.materials[0];
//            for( uint_16 i = 0; i < world.materials.size(); i++ ) {
//                if( world.materials[i]->assignment == groupName ) {
//                    materialToAssign = world.materials[i];
//                    break;
//                }
//            }
//        }
	}
	file.close();

	*verts = new float[numTriangles*3*3];
	*normals = new float[numTriangles*3*3];
	*uvs = new float[numTriangles*3*2];
	for(size_t i = 0; i < trianglesTempList.size(); i++) {
		Triangle* tri = &trianglesTempList[i];

		size_t triOffset = i * 9; // 9 floats per tri

		// Vert 1
		(*verts)[triOffset+0] = vertsTempList[tri->v0].x;
		(*verts)[triOffset+1] = vertsTempList[tri->v0].y;
		(*verts)[triOffset+2] = vertsTempList[tri->v0].z;
		(*normals)[triOffset+0] = normalsTempList[tri->n0].x;
		(*normals)[triOffset+1] = normalsTempList[tri->n0].y;
		(*normals)[triOffset+2] = normalsTempList[tri->n0].z;
		(*uvs)[i*6+0] = uvsTempList[tri->u0].x;
		(*uvs)[i*6+1] = uvsTempList[tri->u0].y;

		// Vert 2
		(*verts)[triOffset+3] = vertsTempList[tri->v1].x;
		(*verts)[triOffset+4] = vertsTempList[tri->v1].y;
		(*verts)[triOffset+5] = vertsTempList[tri->v1].z;
		(*normals)[triOffset+3] = normalsTempList[tri->n1].x;
		(*normals)[triOffset+4] = normalsTempList[tri->n1].y;
		(*normals)[triOffset+5] = normalsTempList[tri->n1].z;
		(*uvs)[i*6+2] = uvsTempList[tri->u1].x;
		(*uvs)[i*6+3] = uvsTempList[tri->u1].y;

		// Vert 3
		(*verts)[triOffset+6] = vertsTempList[tri->v2].x;
		(*verts)[triOffset+7] = vertsTempList[tri->v2].y;
		(*verts)[triOffset+8] = vertsTempList[tri->v2].z;
		(*normals)[triOffset+6] = normalsTempList[tri->n2].x;
		(*normals)[triOffset+7] = normalsTempList[tri->n2].y;
		(*normals)[triOffset+8] = normalsTempList[tri->n2].z;
		(*uvs)[i*6+4] = uvsTempList[tri->u2].x;
		(*uvs)[i*6+5] = uvsTempList[tri->u2].y;
	}

	return numTriangles;
}

vector<string> getNumbersAsStrings(const string& line) {
	int indexStart = line.find(' ');
	int indexEnd;
	vector<string> stringsList;
	while(true) {
		indexStart = line.find_first_not_of(" \r", indexStart);
		if( indexStart == -1 )
			break;

		indexEnd = line.find(' ', indexStart);
		stringsList.push_back(line.substr(indexStart, indexEnd - indexStart));

		indexStart = indexEnd;
	}

	return stringsList;
}

void computeNormal(const Vector3 &v0, const Vector3 &v1, const Vector3 &v2, Vector3 &normal) {
	normal = (v1 - v0).Cross(v2 - v0);
	normal.Normalize();
}

size_t getNormalIndexFromString(const string &str)
{
	return atoi(str.substr(str.find_last_of('/') + 1).c_str());
}

size_t getUVIndexFromString(const string &str)
{
	return atoi(str.substr(str.find_first_of('/') + 1).c_str());
}

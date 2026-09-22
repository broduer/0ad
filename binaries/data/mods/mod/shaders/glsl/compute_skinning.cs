#version 430

#include "common/compute.h"

const int MAX_INFLUENCES = 4;

BEGIN_DRAW_TEXTURES
	NO_DRAW_TEXTURES
END_DRAW_TEXTURES

BEGIN_DRAW_UNIFORMS
	UNIFORM(float, vertexCount)
	// offset.x - input vertex offset
	// offset.y - input skinning data offset
	// offset.z - first instance
	// offset.w - first matrix of the first instance
	UNIFORM(vec4, offset)
	// batch.x - number of instances
	// batch.y - number of matrices per instance
	UNIFORM(vec2, batch)
END_DRAW_UNIFORMS

struct Vertex
{
	vec4 tangent;
	vec4 normal;
	vec4 position;
};

STORAGE_BUFFER(0) restrict readonly buffer InputVertices
{
	Vertex vertices[];
};

STORAGE_BUFFER(1) restrict readonly buffer SkinData
{
	uvec2 skinDatas[];
};

STORAGE_BUFFER(2) writeonly buffer OutputPositions
{
	vec4 outputPositions[];
};

STORAGE_BUFFER(3) writeonly buffer OutputNormalsTangents
{
	uvec4 outputPackedNT[];
};

STORAGE_BUFFER(4) restrict readonly buffer SkinningMatrices
{
	mat4 skinBlendMatrices[];
};

struct Instance
{
	uint positionOffset;
	uint normalTangentOffset;
};

STORAGE_BUFFER(5) restrict readonly buffer SkinningInstances
{
	Instance instances[];
};

uvec4 getSkinJoints(in uvec2 skinData)
{
	return uvec4(unpackUnorm4x8(skinData.y) * 255.0);
}

vec4 getSkinWeights(in uvec2 skinData)
{
	return unpackUnorm4x8(skinData.x);
}

uvec4 packNormalTangent(in vec3 normal, in vec3 tangent, in float weight)
{
	return uvec4(
		packHalf2x16(normal.xy), packHalf2x16(vec2(normal.z, 0.0)),
		packHalf2x16(tangent.xy), packHalf2x16(vec2(tangent.z, weight)));
}

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
void main()
{
	uint index = gl_GlobalInvocationID.x;
	uint instanceIndex = gl_GlobalInvocationID.y;
	if (index >= vertexCount || instanceIndex >= batch.x)
		return;

	uvec4 offsets = uvec4(offset);
	Instance instance = instances[offsets[2] + instanceIndex];
	uint firstMatrix = offsets[3] + instanceIndex * uint(batch.y);

	Vertex vertex = vertices[index + offsets[0]];
	uvec2 skinData = skinDatas[index + offsets[1]];

	vec3 localPosition = vertex.position.xyz;
	vec3 localNormal = vertex.normal.xyz;
	vec3 localTangent = vertex.tangent.xyz;
	uvec4 skinJoints = getSkinJoints(skinData);
	vec4 skinWeights = getSkinWeights(skinData);

	vec3 transformedPosition = vec3(0.0);
	vec3 transformedNormal = vec3(0.0);
	vec3 transformedTangent = vec3(0.0);
	for (uint i = 0; i < MAX_INFLUENCES; ++i)
	{
		uint joint = skinJoints[i];
		if (joint != 0xFF)
		{
			mat4 matrix = skinBlendMatrices[firstMatrix + joint];
			transformedPosition += vec3(matrix * vec4(localPosition, 1.0)) * skinWeights[i];
			transformedNormal += vec3(matrix * vec4(localNormal, 0.0)) * skinWeights[i];
			transformedTangent += vec3(matrix * vec4(localTangent, 0.0)) * skinWeights[i];
		}
	}
	transformedNormal = normalize(transformedNormal);
	transformedTangent = normalize(transformedTangent);

	outputPositions[index + instance.positionOffset] = vec4(transformedPosition, 0.0);
	outputPackedNT[index + instance.normalTangentOffset] = packNormalTangent(transformedNormal, transformedTangent, vertex.tangent.w);
}

/* Copyright (C) 2026 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include "ShaderProgram.h"

#include "graphics/PreprocessorWrapper.h"
#include "graphics/ShaderDefines.h"
#include "lib/config2.h"
#include "lib/path.h"
#include "lib/utf8.h"
#include "ps/CLogger.h"
#include "ps/Errors.h"
#include "ps/Filesystem.h"
#include "ps/Profiler2.h"
#include "ps/XMB/XMBData.h"
#include "ps/XMB/XMBStorage.h"
#include "ps/XML/Xeromyces.h"
#include "renderer/backend/PipelineState.h"
#include "renderer/backend/gl/Buffer.h"
#include "renderer/backend/gl/DeviceCommandContext.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

#define USE_SHADER_XML_VALIDATION 1

#if USE_SHADER_XML_VALIDATION
#include "ps/XML/XMLWriter.h"
#endif

namespace Renderer
{

namespace Backend
{

namespace GL
{

namespace
{

struct Binding
{
	Binding(int a, int b) : first(a), second(b) { }

	Binding() : first(-1), second(-1) { }

	/**
		* Returns whether this uniform attribute is active in the shader.
		* If not then there's no point calling Uniform() to set its value.
		*/
	bool Active() const { return first != -1 || second != -1; }

	int first;
	int second;
};

int GetStreamMask(const VertexAttributeStream stream)
{
	return 1 << static_cast<int>(stream);
}

GLint GLSizeFromFormat(const Format format)
{
	GLint size = 1;
	if (format == Renderer::Backend::Format::R32_SFLOAT ||
		format == Renderer::Backend::Format::R16_SINT ||
		format == Renderer::Backend::Format::R16_SFLOAT)
		size = 1;
	else if (
		format == Renderer::Backend::Format::R8G8_UNORM ||
		format == Renderer::Backend::Format::R8G8_UINT ||
		format == Renderer::Backend::Format::R16G16_SINT ||
		format == Renderer::Backend::Format::R16G16_SFLOAT ||
		format == Renderer::Backend::Format::R32G32_SFLOAT)
		size = 2;
	else if (
		format == Renderer::Backend::Format::R16G16B16_SFLOAT ||
		format == Renderer::Backend::Format::R32G32B32_SFLOAT)
		size = 3;
	else if (
		format == Renderer::Backend::Format::R16G16B16A16_SFLOAT ||
		format == Renderer::Backend::Format::R32G32B32A32_SFLOAT ||
		format == Renderer::Backend::Format::R8G8B8A8_UNORM ||
		format == Renderer::Backend::Format::R8G8B8A8_UINT)
		size = 4;
	else
		debug_warn("Unsupported format.");
	return size;
}

GLenum GLTypeFromFormat(const Format format)
{
	GLenum type = GL_FLOAT;
	if (format == Renderer::Backend::Format::R32_SFLOAT ||
		format == Renderer::Backend::Format::R32G32_SFLOAT ||
		format == Renderer::Backend::Format::R32G32B32_SFLOAT ||
		format == Renderer::Backend::Format::R32G32B32A32_SFLOAT)
		type = GL_FLOAT;
#if !CONFIG2_GLES
	else if (format == Renderer::Backend::Format::R16_SFLOAT ||
		format == Renderer::Backend::Format::R16G16_SFLOAT ||
		format == Renderer::Backend::Format::R16G16B16_SFLOAT ||
		format == Renderer::Backend::Format::R16G16B16A16_SFLOAT)
		type = GL_HALF_FLOAT;
#endif
	else if (
		format == Renderer::Backend::Format::R16_SINT ||
		format == Renderer::Backend::Format::R16G16_SINT)
		type = GL_SHORT;
	else if (
		format == Renderer::Backend::Format::R8G8_UNORM ||
		format == Renderer::Backend::Format::R8G8_UINT ||
		format == Renderer::Backend::Format::R8G8B8A8_UNORM ||
		format == Renderer::Backend::Format::R8G8B8A8_UINT)
		type = GL_UNSIGNED_BYTE;
	else
		debug_warn("Unsupported format.");
	return type;
}

GLboolean NormalizedFromFormat(const Format format)
{
	switch (format)
	{
	case Format::R8G8_UNORM:
	case Format::R8G8B8_UNORM:
	case Format::R8G8B8A8_UNORM:
	case Format::R16_UNORM:
	case Format::R16G16_UNORM:
		return GL_TRUE;
	default:
		break;
	}
	return GL_FALSE;
}

int GetAttributeLocationFromStream(
	CDevice* device, const VertexAttributeStream stream)
{
	// Old mapping makes sense only if we have an old/low-end hardware. Else we
	// need to use sequential numbering to fix #3054. We use presence of
	// compute shaders as a check that the hardware has universal CUs.
	if (device->GetCapabilities().computeShaders)
	{
		return static_cast<int>(stream);
	}
	else
	{
		// Map known semantics onto the attribute locations documented by NVIDIA:
		//  https://download.nvidia.com/developer/Papers/2005/OpenGL_2.0/NVIDIA_OpenGL_2.0_Support.pdf
		//  https://developer.download.nvidia.com/opengl/glsl/glsl_release_notes.pdf
		switch (stream)
		{
		case VertexAttributeStream::POSITION: return 0;
		case VertexAttributeStream::NORMAL: return 2;
		case VertexAttributeStream::COLOR: return 3;
		case VertexAttributeStream::UV0: return 8;
		case VertexAttributeStream::UV1: return 9;
		case VertexAttributeStream::UV2: return 10;
		case VertexAttributeStream::UV3: return 11;
		case VertexAttributeStream::UV4: return 12;
		case VertexAttributeStream::UV5: return 13;
		case VertexAttributeStream::UV6: return 14;
		case VertexAttributeStream::UV7: return 15;
		}
	}

	debug_warn("Invalid attribute semantics");
	return 0;
}

bool PreprocessShaderFile(
	const CShaderDefines& defines, const VfsPath& path, const char* stage,
	CStr& source, std::vector<VfsPath>& fileDependencies)
{
	CVFSFile file;
	if (file.Load(g_VFS, path) != PSRETURN_OK)
	{
		LOGERROR("Failed to load shader file: '%s'", path.string8());
		return false;
	}

	CPreprocessorWrapper preprocessor(
		[&fileDependencies](const CStr& includePath, CStr& out) -> bool
		{
			const VfsPath includeFilePath{L"shaders/glsl/" + wstring_from_utf8(includePath)};
			// Add dependencies anyway to reload the shader when the file is
			// appeared.
			fileDependencies.push_back(includeFilePath);
			CVFSFile includeFile;
			if (includeFile.Load(g_VFS, includeFilePath) != PSRETURN_OK)
			{
				LOGERROR("Failed to load shader include file: '%s'", includeFilePath.string8());
				return false;
			}
			out = includeFile.GetAsString();
			return true;
		});
	preprocessor.AddDefines(defines);
	preprocessor.AddDefine(stage, "1");

#if CONFIG2_GLES
	// GLES defines the macro "GL_ES" in its GLSL preprocessor,
	// but since we run our own preprocessor first, we need to explicitly
	// define it here
	preprocessor.AddDefine("GL_ES", "1");
#endif

	source = preprocessor.Preprocess(file.GetAsString());

	return true;
}

bool CompileGLSL(GLuint shader, const VfsPath& file, const CStr& code)
{
	const char* codeString = code.c_str();
	GLint codeLength = code.length();
	glShaderSource(shader, 1, &codeString, &codeLength);

	glCompileShader(shader);

	GLint ok = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

	GLint length = 0;
	glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

	// Apparently sometimes GL_INFO_LOG_LENGTH is incorrectly reported as 0
	// (http://code.google.com/p/android/issues/detail?id=9953)
	if (!ok && length == 0)
		length = 4096;

	if (length > 1)
	{
		std::unique_ptr<char[]> infolog = std::make_unique<char[]>(length);
		glGetShaderInfoLog(shader, length, nullptr, infolog.get());

		if (ok)
			LOGMESSAGE("Info when compiling shader '%s':\n%s", file.string8(), infolog.get());
		else
			LOGERROR("Failed to compile shader '%s':\n%s", file.string8(), infolog.get());
	}

	return ok;
}

} // anonymous namespace

IDevice* CVertexInputLayout::GetDevice()
{
	return m_Device;
}

CShaderProgram::CShaderProgram(
	CDevice* device, const CStr& name,
	const VfsPath& programPath, std::span<const std::tuple<VfsPath, GLenum>> shaderStages,
	const CShaderDefines& defines,
	const std::map<CStrIntern, int>& vertexAttribs,
	int streamflags) :
	m_StreamFlags(streamflags), m_ValidStreams(0),
	m_Device(device), m_Name(name),
	m_VertexAttribs(vertexAttribs)
{
	for (std::map<CStrIntern, int>::iterator it = m_VertexAttribs.begin(); it != m_VertexAttribs.end(); ++it)
		m_ActiveVertexAttributes.emplace_back(it->second);
	std::sort(m_ActiveVertexAttributes.begin(), m_ActiveVertexAttributes.end());

	m_Program = 0;
	m_FileDependencies = {programPath};
	for (const auto& shaderStage : shaderStages)
		m_FileDependencies.emplace_back(std::get<0>(shaderStage));

	// TODO: replace by scoped bind.
	m_Device->GetActiveCommandContext()->SetGraphicsPipelineState(
		MakeDefaultGraphicsPipelineStateDesc());

	std::vector<VfsPath> newFileDependencies = {programPath};
	for (const auto& [path, type] : shaderStages)
	{
		GLuint shader = glCreateShader(type);
		newFileDependencies.emplace_back(path);
#if !CONFIG2_GLES
		if (m_Device->GetCapabilities().debugLabels)
			glObjectLabel(GL_SHADER, shader, -1, path.string8().c_str());
#endif
		m_ShaderStages.emplace_back(type, shader);
		const char* stageDefine = "STAGE_UNDEFINED";
		switch (type)
		{
		case GL_VERTEX_SHADER:
			stageDefine = "STAGE_VERTEX";
			break;
		case GL_FRAGMENT_SHADER:
			stageDefine = "STAGE_FRAGMENT";
			break;
#if !CONFIG2_GLES
		case GL_COMPUTE_SHADER:
			stageDefine = "STAGE_COMPUTE";
			break;
#endif
		default:
			break;
		}
		CStr source;
		if (!PreprocessShaderFile(defines, path, stageDefine, source, newFileDependencies))
			return;
		if (source.empty())
		{
			LOGERROR("Failed to preprocess shader: '%s'", path.string8());
			return;
		}
#if CONFIG2_GLES
		// Ugly hack to replace desktop GLSL 1.10/1.20 with GLSL ES 1.00,
		// and also to set default float precision for fragment shaders
		source.Replace("#version 110\n", "#version 100\nprecision highp float;\n");
		source.Replace("#version 110\r\n", "#version 100\nprecision highp float;\n");
		source.Replace("#version 120\n", "#version 100\nprecision highp float;\n");
		source.Replace("#version 120\r\n", "#version 100\nprecision highp float;\n");
#endif
		if (!CompileGLSL(shader, path, source))
			return;
	}

	m_FileDependencies = std::move(newFileDependencies);

	if (!Link(programPath))
		return;
}

CShaderProgram::~CShaderProgram()
{
	if (m_Program)
		glDeleteProgram(m_Program);

	for (ShaderStage& stage : m_ShaderStages)
		glDeleteShader(stage.shader);
}

bool CShaderProgram::Link(const VfsPath& path)
{
	ENSURE(!m_Program);
	m_Program = glCreateProgram();

#if !CONFIG2_GLES
	if (m_Device->GetCapabilities().debugLabels)
	{
		glObjectLabel(GL_PROGRAM, m_Program, -1, m_Name.c_str());
	}
#endif

	for (ShaderStage& stage : m_ShaderStages)
	{
		glAttachShader(m_Program, stage.shader);
	}

	// Set up the attribute bindings explicitly, since apparently drivers
	// don't always pick the most efficient bindings automatically,
	// and also this lets us hardcode indexes into VertexPointer etc
	for (std::map<CStrIntern, int>::iterator it = m_VertexAttribs.begin(); it != m_VertexAttribs.end(); ++it)
		glBindAttribLocation(m_Program, it->second, it->first.c_str());

	glLinkProgram(m_Program);

	GLint ok = 0;
	glGetProgramiv(m_Program, GL_LINK_STATUS, &ok);

	GLint length = 0;
	glGetProgramiv(m_Program, GL_INFO_LOG_LENGTH, &length);

	if (!ok && length == 0)
		length = 4096;

	if (length > 1)
	{
		char* infolog = new char[length];
		glGetProgramInfoLog(m_Program, length, NULL, infolog);

		if (ok)
			LOGMESSAGE("Info when linking program '%s':\n%s", path.string8(), infolog);
		else
			LOGERROR("Failed to link program '%s':\n%s", path.string8(), infolog);

		delete[] infolog;
	}

	if (!ok)
		return false;

	Bind(nullptr);

	// Reorder sampler units to decrease redundant texture unit changes when
	// samplers bound in a different order.
	const std::unordered_map<CStrIntern, int> requiredUnits =
	{
		{CStrIntern("baseTex"), 0},
		{CStrIntern("normTex"), 1},
		{CStrIntern("specTex"), 2},
		{CStrIntern("aoTex"), 3},
		{CStrIntern("shadowTex"), 4},
		{CStrIntern("losTex"), 5},
	};

	std::vector<uint8_t> occupiedUnits;

#if !CONFIG2_GLES
	const bool isStorageSupported{m_Device->GetCapabilities().storage};
	if (isStorageSupported)
	{
		constexpr GLint maxBlockNameLength{128};
		char name[maxBlockNameLength];

		GLint maxUniformBlockNameLength{0};
		glGetProgramInterfaceiv(m_Program, GL_UNIFORM_BLOCK, GL_MAX_NAME_LENGTH, &maxUniformBlockNameLength);

		GLint numberOfActiveUniformBlocks{0};
		glGetProgramInterfaceiv(m_Program, GL_UNIFORM_BLOCK, GL_ACTIVE_RESOURCES, &numberOfActiveUniformBlocks);
		// Currently we support the only one uniform buffer per shader.
		if (numberOfActiveUniformBlocks == 1)
		{
			GLsizei length{0};
			glGetProgramResourceName(m_Program, GL_UNIFORM_BLOCK, 0, maxBlockNameLength, &length, name);

			const GLuint location{glGetProgramResourceIndex(m_Program, GL_UNIFORM_BLOCK, name)};
			glUniformBlockBinding(m_Program, location, location);

			m_UniformBufferLocation = location;
		}

		GLint maxStorageNameLength{0};
		glGetProgramInterfaceiv(m_Program, GL_SHADER_STORAGE_BLOCK, GL_MAX_NAME_LENGTH, &maxStorageNameLength);
		ENSURE(maxStorageNameLength <= maxBlockNameLength);
		GLint numberOfActiveStorages{0};
		glGetProgramInterfaceiv(m_Program, GL_SHADER_STORAGE_BLOCK, GL_ACTIVE_RESOURCES, &numberOfActiveStorages);
		for (GLint index{0}; index < numberOfActiveStorages; ++index)
		{
			GLsizei length{0};
			glGetProgramResourceName(m_Program, GL_SHADER_STORAGE_BLOCK, index, maxBlockNameLength, &length, name);

			const GLuint location{glGetProgramResourceIndex(m_Program, GL_SHADER_STORAGE_BLOCK, name)};
			glShaderStorageBlockBinding(m_Program, location, location);

			const CStrIntern nameIntern(name);

			m_BindingSlotsMapping[nameIntern] = m_BindingSlots.size();
			BindingSlot bindingSlot{};
			bindingSlot.name = nameIntern;
			bindingSlot.location = location;
			bindingSlot.isStorageBuffer = true;
			m_BindingSlots.emplace_back(std::move(bindingSlot));
		}
	}
#endif

	GLint numUniforms = 0;
	glGetProgramiv(m_Program, GL_ACTIVE_UNIFORMS, &numUniforms);
	for (GLint i = 0; i < numUniforms; ++i)
	{
		// TODO: use GL_ACTIVE_UNIFORM_MAX_LENGTH for the size.
		char name[256] = {0};
		GLsizei nameLength = 0;
		GLint size = 0;
		GLenum type = 0;
		glGetActiveUniform(m_Program, i, ARRAY_SIZE(name), &nameLength, &size, &type, name);

		const GLint location = glGetUniformLocation(m_Program, name);

		// OpenGL specification is a bit vague about a name returned by glGetActiveUniform.
		// NVIDIA drivers return uniform name with "[0]", Intel Windows drivers without;
		while (nameLength > 3 &&
			name[nameLength - 3] == '[' &&
			name[nameLength - 2] == '0' &&
			name[nameLength - 1] == ']')
		{
			nameLength -= 3;
		}
		name[nameLength] = 0;

		const CStrIntern nameIntern(name);

		m_BindingSlotsMapping[nameIntern] = m_BindingSlots.size();
		BindingSlot bindingSlot{};
		bindingSlot.name = nameIntern;
		bindingSlot.location = location;
		bindingSlot.size = size;
		bindingSlot.type = type;
		bindingSlot.isTexture = false;
		bindingSlot.isStorageBuffer = false;

#define CASE(TYPE, ELEMENT_TYPE, ELEMENT_COUNT) \
			case GL_ ## TYPE: \
				bindingSlot.elementType = GL_ ## ELEMENT_TYPE; \
				bindingSlot.elementCount = ELEMENT_COUNT; \
				break;

		switch (type)
		{
			CASE(FLOAT, FLOAT, 1);
			CASE(FLOAT_VEC2, FLOAT, 2);
			CASE(FLOAT_VEC3, FLOAT, 3);
			CASE(FLOAT_VEC4, FLOAT, 4);
			CASE(INT, INT, 1);
			CASE(FLOAT_MAT2, FLOAT, 4);
			CASE(FLOAT_MAT3, FLOAT, 9);
			CASE(FLOAT_MAT4, FLOAT, 16);
#if !CONFIG2_GLES // GL ES 2.0 doesn't support non-square matrices.
			CASE(FLOAT_MAT2x3, FLOAT, 6);
			CASE(FLOAT_MAT2x4, FLOAT, 8);
			CASE(FLOAT_MAT3x2, FLOAT, 6);
			CASE(FLOAT_MAT3x4, FLOAT, 12);
			CASE(FLOAT_MAT4x2, FLOAT, 8);
			CASE(FLOAT_MAT4x3, FLOAT, 12);
#endif
		}
#undef CASE

		// Assign sampler uniforms to sequential texture units.
		switch (type)
		{
		case GL_SAMPLER_2D:
			bindingSlot.elementType = GL_TEXTURE_2D;
			bindingSlot.isTexture = true;
			break;
		case GL_SAMPLER_CUBE:
			bindingSlot.elementType = GL_TEXTURE_CUBE_MAP;
			bindingSlot.isTexture = true;
			break;
#if !CONFIG2_GLES
		case GL_SAMPLER_2D_SHADOW:
			bindingSlot.elementType = GL_TEXTURE_2D;
			bindingSlot.isTexture = true;
			break;
		case GL_IMAGE_2D:
			bindingSlot.elementType = GL_IMAGE_2D;
			bindingSlot.isTexture = true;
			m_HasImageUniforms = true;
			break;
#endif
		default:
			break;
		}

		if (bindingSlot.isTexture)
		{
			const auto it = requiredUnits.find(nameIntern);
			const int unit = it == requiredUnits.end() ? -1 : it->second;
			bindingSlot.elementCount = unit;
			if (unit != -1)
			{
				if (unit >= static_cast<int>(occupiedUnits.size()))
					occupiedUnits.resize(unit + 1);
				occupiedUnits[unit] = true;
			}
		}

		if (bindingSlot.elementType == 0)
		{
			LOGERROR("CShaderProgram::Link: unsupported uniform type: 0x%04x", static_cast<int>(type));
		}

#if !CONFIG2_GLES
		if (isStorageSupported)
		{
			GLuint uniformIndex{0};
			const GLchar* nameToQuery{name};
			glGetUniformIndices(m_Program, 1, &nameToQuery, &uniformIndex);

			GLint uniformOffset{0};
			glGetActiveUniformsiv(m_Program, 1, &uniformIndex, GL_UNIFORM_OFFSET, &uniformOffset);

			// According to the OpenGL spec:
			//  https://registry.khronos.org/OpenGL-Refpages/es3/html/glGetActiveUniformsiv.xhtml
			//  For uniforms in the default uniform block, -1 will be returned.
			if (uniformOffset >= 0)
			{
				const uint32_t sizeInBytes{static_cast<uint32_t>(bindingSlot.size * bindingSlot.elementCount * sizeof(float))};
				m_UniformBufferSize = std::max(m_UniformBufferSize, uniformOffset + sizeInBytes);
				bindingSlot.location = -1;
				bindingSlot.offset = uniformOffset;
			}
		}
#endif

		m_BindingSlots.emplace_back(std::move(bindingSlot));
	}

	for (BindingSlot& bindingSlot : m_BindingSlots)
	{
		if (!bindingSlot.isTexture)
			continue;
		if (bindingSlot.elementCount == -1)
		{
			// We need to find a minimal available unit.
			int unit = 0;
			while (unit < static_cast<int>(occupiedUnits.size()) && occupiedUnits[unit])
				++unit;
			if (unit >= static_cast<int>(occupiedUnits.size()))
				occupiedUnits.resize(unit + 1);
			occupiedUnits[unit] = true;
			bindingSlot.elementCount = unit;
		}
		// Link uniform to unit.
		glUniform1i(bindingSlot.location, bindingSlot.elementCount);
	}

	if (m_UniformBufferSize > 0 && m_UniformBufferLocation != -1)
	{
		m_UniformBuffer = m_Device->CreateBuffer(
			"ShaderProgramUniformBuffer", IBuffer::Type::UNIFORM, m_UniformBufferSize,
			IBuffer::Usage::DYNAMIC | IBuffer::Usage::TRANSFER_DST);
	}

	// TODO: verify that we're not using more samplers than is supported

	Unbind();

	return true;
}

void CShaderProgram::Bind(CShaderProgram* previousShaderProgram)
{
	ENSURE(this != previousShaderProgram);

	glUseProgram(m_Program);
#if !CONFIG2_GLES
	if (m_UniformBuffer)
		glBindBufferBase(GL_UNIFORM_BUFFER, m_UniformBufferLocation, m_UniformBuffer->As<CBuffer>()->GetHandle());
#endif

	if (previousShaderProgram)
	{
		// Attribute array state is shared across programs.
		for (const int index : m_ActiveVertexAttributes)
			if (previousShaderProgram->m_ConstantAttributes & (1u << index))
				glEnableVertexAttribArray(index);
		previousShaderProgram->m_ConstantAttributes = 0;

		std::vector<int>::iterator itPrevious = previousShaderProgram->m_ActiveVertexAttributes.begin();
		std::vector<int>::iterator itNext = m_ActiveVertexAttributes.begin();
		while (
			itPrevious != previousShaderProgram->m_ActiveVertexAttributes.end() ||
			itNext != m_ActiveVertexAttributes.end())
		{
			if (itPrevious != previousShaderProgram->m_ActiveVertexAttributes.end() &&
				itNext != m_ActiveVertexAttributes.end())
			{
				if (*itPrevious == *itNext)
				{
					++itPrevious;
					++itNext;
				}
				else if (*itPrevious < *itNext)
				{
					glDisableVertexAttribArray(*itPrevious);
					++itPrevious;
				}
				else if (*itPrevious > *itNext)
				{
					glEnableVertexAttribArray(*itNext);
					++itNext;
				}
			}
			else if (itPrevious != previousShaderProgram->m_ActiveVertexAttributes.end())
			{
				glDisableVertexAttribArray(*itPrevious);
				++itPrevious;
			}
			else if (itNext != m_ActiveVertexAttributes.end())
			{
				glEnableVertexAttribArray(*itNext);
				++itNext;
			}
		}
	}
	else
	{
		for (const int index : m_ActiveVertexAttributes)
			glEnableVertexAttribArray(index);
	}

	m_ValidStreams = 0;
}

void CShaderProgram::Unbind()
{
	glUseProgram(0);

	for (const int index : m_ActiveVertexAttributes)
		glDisableVertexAttribArray(index);
	m_ConstantAttributes = 0;
}

int32_t CShaderProgram::GetBindingSlot(const CStrIntern name) const
{
	auto it = m_BindingSlotsMapping.find(name);
	return it == m_BindingSlotsMapping.end() ? -1 : it->second;
}

CShaderProgram::TextureUnit CShaderProgram::GetTextureUnit(const int32_t bindingSlot)
{
	if (bindingSlot < 0 || bindingSlot >= static_cast<int32_t>(m_BindingSlots.size()))
		return { 0, 0, 0 };
	TextureUnit textureUnit;
	textureUnit.type = m_BindingSlots[bindingSlot].type;
	textureUnit.target = m_BindingSlots[bindingSlot].elementType;
	textureUnit.unit = m_BindingSlots[bindingSlot].elementCount;
	return textureUnit;
}

GLuint CShaderProgram::GetStorageBuffer(const int32_t bindingSlot)
{
	if (bindingSlot < 0 || bindingSlot >= static_cast<int32_t>(m_BindingSlots.size()))
		return 0;
	if (!m_BindingSlots[bindingSlot].isStorageBuffer)
		LOGERROR("CShaderProgramGLSL::GetStorageBuffer(): Invalid slot (expected storage buffer): '%s'", m_BindingSlots[bindingSlot].name.c_str());
	return m_BindingSlots[bindingSlot].location;
}

void CShaderProgram::SetUniform(const int32_t bindingSlot, const float value)
{
	if (bindingSlot < 0 || bindingSlot >= static_cast<int32_t>(m_BindingSlots.size()))
		return;
	if (m_BindingSlots[bindingSlot].type != GL_FLOAT ||
		m_BindingSlots[bindingSlot].size != 1)
	{
		LOGERROR("CShaderProgramGLSL::SetUniform(): Invalid uniform type (expected float) '%s'", m_BindingSlots[bindingSlot].name.c_str());
		return;
	}
	glUniform1f(m_BindingSlots[bindingSlot].location, value);
}

void CShaderProgram::SetUniform(
	const int32_t bindingSlot,
	const float valueX, const float valueY)
{
	if (bindingSlot < 0 || bindingSlot >= static_cast<int32_t>(m_BindingSlots.size()))
		return;
	if (m_BindingSlots[bindingSlot].type != GL_FLOAT_VEC2 ||
		m_BindingSlots[bindingSlot].size != 1)
	{
		LOGERROR("CShaderProgramGLSL::SetUniform(): Invalid uniform type (expected vec2) '%s'", m_BindingSlots[bindingSlot].name.c_str());
		return;
	}
	glUniform2f(m_BindingSlots[bindingSlot].location, valueX, valueY);
}

void CShaderProgram::SetUniform(
	const int32_t bindingSlot,
	const float valueX, const float valueY, const float valueZ)
{
	if (bindingSlot < 0 || bindingSlot >= static_cast<int32_t>(m_BindingSlots.size()))
		return;
	if (m_BindingSlots[bindingSlot].type != GL_FLOAT_VEC3 ||
		m_BindingSlots[bindingSlot].size != 1)
	{
		LOGERROR("CShaderProgramGLSL::SetUniform(): Invalid uniform type (expected vec3) '%s'", m_BindingSlots[bindingSlot].name.c_str());
		return;
	}
	glUniform3f(m_BindingSlots[bindingSlot].location, valueX, valueY, valueZ);
}

void CShaderProgram::SetUniform(
	const int32_t bindingSlot,
	const float valueX, const float valueY,
	const float valueZ, const float valueW)
{
	if (bindingSlot < 0 || bindingSlot >= static_cast<int32_t>(m_BindingSlots.size()))
		return;
	if (m_BindingSlots[bindingSlot].type != GL_FLOAT_VEC4 ||
		m_BindingSlots[bindingSlot].size != 1)
	{
		LOGERROR("CShaderProgram::SetUniform(): Invalid uniform type (expected vec4) '%s'", m_BindingSlots[bindingSlot].name.c_str());
		return;
	}
	glUniform4f(m_BindingSlots[bindingSlot].location, valueX, valueY, valueZ, valueW);
}

void CShaderProgram::SetUniform(
	const int32_t bindingSlot, std::span<const float> values)
{
	if (bindingSlot < 0 || bindingSlot >= static_cast<int32_t>(m_BindingSlots.size()))
		return;
	if (m_BindingSlots[bindingSlot].elementType != GL_FLOAT)
	{
		LOGERROR("CShaderProgram::SetUniform(): Invalid uniform element type (expected float) '%s'", m_BindingSlots[bindingSlot].name.c_str());
		return;
	}
	if (m_BindingSlots[bindingSlot].size == 1 && m_BindingSlots[bindingSlot].elementCount > static_cast<GLint>(values.size()))
	{
		LOGERROR(
			"CShaderProgram::SetUniform(): Invalid uniform element count (expected: %zu passed: %zu) '%s'",
			m_BindingSlots[bindingSlot].elementCount, values.size(), m_BindingSlots[bindingSlot].name.c_str());
		return;
	}
	const GLint location = m_BindingSlots[bindingSlot].location;
	const GLenum type = m_BindingSlots[bindingSlot].type;

	if (location == -1)
	{
		const uint32_t sizeInBytes{
			static_cast<uint32_t>(m_BindingSlots[bindingSlot].size * m_BindingSlots[bindingSlot].elementCount * sizeof(float))};
		const uint32_t dataSizeToUpload{std::min(
			static_cast<uint32_t>(values.size() * sizeof(float)), sizeInBytes)};
		m_Device->GetActiveCommandContext()->UploadBufferRegion(
			m_UniformBuffer.get(), values.data(), m_BindingSlots[bindingSlot].offset, dataSizeToUpload);
		return;
	}

	if (type == GL_FLOAT)
		glUniform1fv(location, 1, values.data());
	else if (type == GL_FLOAT_VEC2)
		glUniform2fv(location, 1, values.data());
	else if (type == GL_FLOAT_VEC3)
		glUniform3fv(location, 1, values.data());
	else if (type == GL_FLOAT_VEC4)
		glUniform4fv(location, 1, values.data());
	else if (type == GL_FLOAT_MAT4)
	{
		// For case of array of matrices we might pass less number of matrices.
		const GLint size = std::min(
			m_BindingSlots[bindingSlot].size, static_cast<GLint>(values.size() / 16));
		glUniformMatrix4fv(location, size, GL_FALSE, values.data());
	}
	else
		LOGERROR("CShaderProgram::SetUniform(): Invalid uniform type (expected float, vec2, vec3, vec4, mat4) '%s'", m_BindingSlots[bindingSlot].name.c_str());
}

void CShaderProgram::VertexAttribPointer(
	const VertexAttributeStream stream, const Format format,
	const uint32_t offset, const uint32_t stride,
	[[maybe_unused]] const VertexAttributeRate rate, const void* data)
{
	const int attributeLocation = GetAttributeLocationFromStream(m_Device, stream);
	std::vector<int>::const_iterator it =
		std::lower_bound(m_ActiveVertexAttributes.begin(), m_ActiveVertexAttributes.end(), attributeLocation);
	if (it == m_ActiveVertexAttributes.end() || *it != attributeLocation)
		return;

	ENSURE(attributeLocation < 32);
	const uint32_t attributeBit = 1u << attributeLocation;
	if (m_ConstantAttributes & attributeBit)
	{
		glEnableVertexAttribArray(attributeLocation);
		m_ConstantAttributes &= ~attributeBit;
	}
	const GLint size = GLSizeFromFormat(format);
	const GLenum type = GLTypeFromFormat(format);
	const GLboolean normalized = NormalizedFromFormat(format);
	glVertexAttribPointer(
		attributeLocation, size, type, normalized, stride,
		static_cast<const std::uint8_t*>(data) + offset);
#if CONFIG2_GLES
	ENSURE(rate == VertexAttributeRate::PER_VERTEX);
#else
	if (m_Device->GetCapabilities().instancing)
		glVertexAttribDivisorARB(attributeLocation, rate == VertexAttributeRate::PER_INSTANCE ? 1 : 0);
	else
		ENSURE(rate == VertexAttributeRate::PER_VERTEX);
#endif
	m_ValidStreams |= GetStreamMask(stream);
}

void CShaderProgram::VertexAttribConstant(
	const VertexAttributeStream stream, const Format format, const void* value)
{
	const int attributeLocation = GetAttributeLocationFromStream(m_Device, stream);
	if (!std::ranges::binary_search(m_ActiveVertexAttributes, attributeLocation))
		return;

	ENSURE(attributeLocation < 32);
	ENSURE(format == Format::R32G32B32A32_SFLOAT);
	glDisableVertexAttribArray(attributeLocation);
	glVertexAttrib4fv(attributeLocation, static_cast<const GLfloat*>(value));
	m_ConstantAttributes |= 1u << attributeLocation;
	m_ValidStreams |= GetStreamMask(stream);
}

std::vector<VfsPath> CShaderProgram::GetFileDependencies() const
{
	return m_FileDependencies;
}

// static
std::unique_ptr<CShaderProgram> CShaderProgram::Create(CDevice* device, const CStr& name, const CShaderDefines& baseDefines)
{
	PROFILE2("loading shader");
	PROFILE2_ATTR("name: %s", name.c_str());

	VfsPath xmlFilename = L"shaders/" + wstring_from_utf8(name) + L".xml";

	CXeromyces XeroFile;
	PSRETURN ret = XeroFile.Load(g_VFS, xmlFilename);
	if (ret != PSRETURN_OK)
		return nullptr;

#if USE_SHADER_XML_VALIDATION
	{
		// Serialize the XMB data and pass it to the validator
		XMLWriter_File shaderFile;
		shaderFile.SetPrettyPrint(false);
		shaderFile.XMB(XeroFile);
		if (!g_Xeromyces.ValidateEncoded("shader", name, shaderFile.GetOutput()))
			return nullptr;
	}
#endif

	// Define all the elements and attributes used in the XML file
#define EL(x) int el_##x = XeroFile.GetElementID(#x)
#define AT(x) int at_##x = XeroFile.GetAttributeID(#x)
	EL(compute);
	EL(define);
	EL(fragment);
	EL(stream);
	EL(uniform);
	EL(vertex);
	AT(attribute);
	AT(file);
	AT(if);
	AT(loc);
	AT(name);
	AT(type);
	AT(value);
#undef AT
#undef EL

	CPreprocessorWrapper preprocessor;
	preprocessor.AddDefines(baseDefines);

	XMBElement root = XeroFile.GetRoot();

	VfsPath vertexFile;
	VfsPath fragmentFile;
	CShaderDefines defines = baseDefines;
	std::map<CStrIntern, std::pair<CStr, int>> vertexUniforms;
	std::map<CStrIntern, std::pair<CStr, int>> fragmentUniforms;
	std::map<CStrIntern, int> vertexAttribs;
	int streamFlags = 0;

	VfsPath computeFile;

	for (XMBElement child : root.GetChildNodes())
	{
		if (child.GetNodeName() == el_define)
		{
			defines.Add(CStrIntern(child.GetAttributes().GetNamedItem(at_name)), CStrIntern(child.GetAttributes().GetNamedItem(at_value)));
		}
		else if (child.GetNodeName() == el_vertex)
		{
			vertexFile = L"shaders/" + child.GetAttributes().GetNamedItem(at_file).FromUTF8();

			for (XMBElement param : child.GetChildNodes())
			{
				XMBAttributeList attributes = param.GetAttributes();

				CStr cond = attributes.GetNamedItem(at_if);
				if (!cond.empty() && !preprocessor.TestConditional(cond))
					continue;

				if (param.GetNodeName() == el_uniform)
				{
					vertexUniforms[CStrIntern(attributes.GetNamedItem(at_name))] =
						std::make_pair(attributes.GetNamedItem(at_type), attributes.GetNamedItem(at_loc).ToInt());
				}
				else if (param.GetNodeName() == el_stream)
				{
					const CStr streamName = attributes.GetNamedItem(at_name);
					const CStr attributeName = attributes.GetNamedItem(at_attribute);
					if (attributeName.empty())
						LOGERROR("Empty attribute name in vertex shader description '%s'", vertexFile.string8().c_str());

					VertexAttributeStream stream =
						VertexAttributeStream::UV7;
					if (streamName == "pos")
						stream = VertexAttributeStream::POSITION;
					else if (streamName == "normal")
						stream = VertexAttributeStream::NORMAL;
					else if (streamName == "color")
						stream = VertexAttributeStream::COLOR;
					else if (streamName == "uv0")
						stream = VertexAttributeStream::UV0;
					else if (streamName == "uv1")
						stream = VertexAttributeStream::UV1;
					else if (streamName == "uv2")
						stream = VertexAttributeStream::UV2;
					else if (streamName == "uv3")
						stream = VertexAttributeStream::UV3;
					else if (streamName == "uv4")
						stream = VertexAttributeStream::UV4;
					else if (streamName == "uv5")
						stream = VertexAttributeStream::UV5;
					else if (streamName == "uv6")
						stream = VertexAttributeStream::UV6;
					else if (streamName == "uv7")
						stream = VertexAttributeStream::UV7;
					else
						LOGERROR("Unknown stream '%s' in vertex shader description '%s'", streamName.c_str(), vertexFile.string8().c_str());

					const int attributeLocation = GetAttributeLocationFromStream(device, stream);
					vertexAttribs[CStrIntern(attributeName)] = attributeLocation;
					streamFlags |= GetStreamMask(stream);
				}
			}
		}
		else if (child.GetNodeName() == el_fragment)
		{
			fragmentFile = L"shaders/" + child.GetAttributes().GetNamedItem(at_file).FromUTF8();

			for (XMBElement param : child.GetChildNodes())
			{
				XMBAttributeList attributes = param.GetAttributes();

				CStr cond = attributes.GetNamedItem(at_if);
				if (!cond.empty() && !preprocessor.TestConditional(cond))
					continue;

				if (param.GetNodeName() == el_uniform)
				{
					fragmentUniforms[CStrIntern(attributes.GetNamedItem(at_name))] =
						std::make_pair(attributes.GetNamedItem(at_type), attributes.GetNamedItem(at_loc).ToInt());
				}
			}
		}
		else if (child.GetNodeName() == el_compute)
		{
			computeFile = L"shaders/" + child.GetAttributes().GetNamedItem(at_file).FromUTF8();
		}
	}

#if !CONFIG2_GLES
	if (!computeFile.empty())
	{
		ENSURE(streamFlags == 0);
		ENSURE(vertexAttribs.empty());
	}
	const PS::StaticVector<std::tuple<VfsPath, GLenum>, 2> shaderStages{computeFile.empty()
		? PS::StaticVector<std::tuple<VfsPath, GLenum>, 2>{{vertexFile, GL_VERTEX_SHADER}, {fragmentFile, GL_FRAGMENT_SHADER}}
		: PS::StaticVector<std::tuple<VfsPath, GLenum>, 2>{{computeFile, GL_COMPUTE_SHADER}}};
#else
	const PS::StaticVector<std::tuple<VfsPath, GLenum>, 2> shaderStages{{{vertexFile, GL_VERTEX_SHADER}, {fragmentFile, GL_FRAGMENT_SHADER}}};
#endif

	return std::unique_ptr<CShaderProgram>(new CShaderProgram(
		device, name, xmlFilename, shaderStages, defines,
		vertexAttribs, streamFlags));
}

bool CShaderProgram::IsStreamActive(const VertexAttributeStream stream) const
{
	return (m_StreamFlags & GetStreamMask(stream)) != 0;
}

void CShaderProgram::AssertPointersBound()
{
	ENSURE((m_StreamFlags & ~m_ValidStreams) == 0);
}

} // namespace GL

} // namespace Backend

} // namespace Renderer

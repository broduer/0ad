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

#ifndef INCLUDED_RENDERER_BACKEND_GL_SHADERPROGRAM
#define INCLUDED_RENDERER_BACKEND_GL_SHADERPROGRAM

#include "lib/code_annotation.h"
#include "lib/debug.h"
#include "ps/containers/StaticVector.h"
#include "ps/CStr.h"
#include "ps/CStrIntern.h"
#include "renderer/backend/Format.h"
#include "renderer/backend/gl/Device.h"
#include "renderer/backend/IBuffer.h"
#include "renderer/backend/IShaderProgram.h"

#if CONFIG2_GLES
#include "external_libraries/opengles2_wrapper.h"
#else
#include <glad/gl.h>
#endif

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <tuple>
#include <unordered_map>
#include <vector>

class CShaderDefines;
class CStrIntern;
namespace Renderer::Backend::GL { class CDevice; }

namespace Renderer
{

namespace Backend
{

namespace GL
{

class CVertexInputLayout : public IVertexInputLayout
{
public:
	CVertexInputLayout(CDevice* device, const std::span<const SVertexAttributeFormat> attributes)
		: m_Device(device), m_Attributes(attributes.begin(), attributes.end())
	{
		for (const SVertexAttributeFormat& attribute : m_Attributes)
		{
			ENSURE(attribute.format != Format::UNDEFINED);
			ENSURE(attribute.stride > 0);
		}
	}

	~CVertexInputLayout() override = default;

	IDevice* GetDevice() override;

	const std::vector<SVertexAttributeFormat>& GetAttributes() const noexcept { return m_Attributes; }

private:
	CDevice* m_Device = nullptr;

	std::vector<SVertexAttributeFormat> m_Attributes;
};

/**
 * A compiled vertex+fragment shader program.
 *
 * Setting uniforms that the shader doesn't support is harmless.
 */
class CShaderProgram final : public IShaderProgram
{
	NONCOPYABLE(CShaderProgram);

public:
	typedef CStrIntern attrib_id_t;

	static std::unique_ptr<CShaderProgram> Create(
		CDevice* device, const CStr& name, const CShaderDefines& baseDefines);

	~CShaderProgram() override;

	/**
	 * Binds the shader into the GL context. Call this before calling Uniform()
	 * or trying to render with it.
	 */
	void Bind(CShaderProgram* previousShaderProgram);

	/**
	 * Unbinds the shader from the GL context. Call this after rendering with it.
	 */
	void Unbind();

	int32_t GetBindingSlot(const CStrIntern name) const override;

	struct TextureUnit
	{
		GLenum type;
		GLenum target;
		GLint unit;
	};
	TextureUnit GetTextureUnit(const int32_t bindingSlot);

	GLuint GetStorageBuffer(const int32_t bindingSlot);

	void SetUniform(
		const int32_t bindingSlot,
		const float value);
	void SetUniform(
		const int32_t bindingSlot,
		const float valueX, const float valueY);
	void SetUniform(
		const int32_t bindingSlot,
		const float valueX, const float valueY,
		const float valueZ);
	void SetUniform(
		const int32_t bindingSlot,
		const float valueX, const float valueY,
		const float valueZ, const float valueW);
	void SetUniform(
		const int32_t bindingSlot, std::span<const float> values);

	// Vertex attribute pointers (equivalent to glVertexPointer etc).
	void VertexAttribPointer(
		const VertexAttributeStream stream, const Format format,
		const uint32_t offset, const uint32_t stride,
		const VertexAttributeRate rate, const void* data);

	/**
	 * Set the stream to a constant R32G32B32A32_SFLOAT value.
	 */
	void VertexAttribConstant(
		const VertexAttributeStream stream, const Format format, const void* value);

	bool IsStreamActive(const VertexAttributeStream stream) const;

	bool HasImageUniforms() const { return m_HasImageUniforms; }

	IDevice* GetDevice() override { return m_Device; }

	std::vector<VfsPath> GetFileDependencies() const override;

	/**
	 * Checks that all the required vertex attributes have been set.
	 * Call this before calling Draw/DrawIndexed etc to avoid potential crashes.
	 */
	void AssertPointersBound();

private:
	CShaderProgram(
		CDevice* device, const CStr& name, const VfsPath& programPath,
		std::span<const std::tuple<VfsPath, GLenum>> shaderStages,
		const CShaderDefines& defines,
		const std::map<CStrIntern, int>& vertexAttribs,
		int streamflags);

	bool Link(const VfsPath& path);

	int m_StreamFlags;

	int m_ValidStreams; // which streams have been specified via VertexPointer etc since the last Bind

	// Bitmask of constant attributes.
	uint32_t m_ConstantAttributes = 0;

	bool m_HasImageUniforms{false};

	struct ShaderStage
	{
		GLenum type;
		GLuint shader;
	};

	CDevice* m_Device{nullptr};

	CStr m_Name;
	std::vector<VfsPath> m_FileDependencies;

	std::map<CStrIntern, int> m_VertexAttribs;
	// Sorted list of active vertex attributes.
	std::vector<int> m_ActiveVertexAttributes;

	GLuint m_Program;
	// 5 = max(compute, vertex + tesselation (control + evaluation) + geometry + fragment).
	PS::StaticVector<ShaderStage, 5> m_ShaderStages;

	struct BindingSlot
	{
		CStrIntern name;
		GLint location;
		GLint offset;
		GLint size;
		GLenum type;
		GLenum elementType;
		GLint elementCount;
		bool isTexture;
		bool isStorageBuffer;
	};
	std::vector<BindingSlot> m_BindingSlots;
	std::unordered_map<CStrIntern, int32_t> m_BindingSlotsMapping;

	GLint m_UniformBufferLocation{-1};
	uint32_t m_UniformBufferSize{0};
	std::unique_ptr<IBuffer> m_UniformBuffer;
};

} // namespace GL

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_GL_SHADERPROGRAM

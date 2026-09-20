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

#ifndef INCLUDED_RENDERER_BACKEND_FORMAT
#define INCLUDED_RENDERER_BACKEND_FORMAT

#include <cstdint>

namespace Renderer
{

namespace Backend
{

enum class Format
{
	UNDEFINED,
	R8_UNORM,
	R8G8_UNORM,
	R8G8_UINT,
	R8G8B8_UNORM,
	R8G8B8A8_UNORM,
	R8G8B8A8_SRGB,
	R8G8B8A8_UINT,
	B8G8R8A8_UNORM,
	B8G8R8A8_SRGB,

	// TODO: we need to drop legacy A8 and L8 formats as soon as we have proper
	// channel swizzling.
	A8_UNORM,
	L8_UNORM,

	R16_UNORM,
	R16_UINT,
	R16_SINT,
	R16_SFLOAT,
	R16G16_UNORM,
	R16G16_UINT,
	R16G16_SINT,
	R16G16_SFLOAT,
	R16G16B16_SFLOAT,
	R16G16B16A16_SFLOAT,

	R32_SFLOAT,
	R32G32_SFLOAT,
	R32G32B32_SFLOAT,
	R32G32B32A32_SFLOAT,

	B10G11R11_UFLOAT,

	D16_UNORM,
	D24_UNORM,
	D24_UNORM_S8_UINT,
	D32_SFLOAT,
	D32_SFLOAT_S8_UINT,

	BC1_RGB_UNORM,
	BC1_RGB_SRGB,
	BC1_RGBA_UNORM,
	BC1_RGBA_SRGB,
	BC2_UNORM,
	BC2_SRGB,
	BC3_UNORM,
	BC3_SRGB,
};

inline bool IsDepthFormat(const Format format)
{
	return
		format == Format::D16_UNORM ||
		format == Format::D24_UNORM ||
		format == Format::D24_UNORM_S8_UINT ||
		format == Format::D32_SFLOAT ||
		format == Format::D32_SFLOAT_S8_UINT;
}

/**
 * Return the attribute size of the format, or zero if unsupported.
 */
constexpr uint32_t GetVertexAttributeSize(const Format format)
{
	switch (format)
	{
	case Format::R8G8B8A8_UNORM:
	case Format::R8G8B8A8_UINT:
		return sizeof(std::uint8_t) * 4;
	case Format::A8_UNORM:
	case Format::R8_UNORM:
		return sizeof(std::uint8_t);
	case Format::R8G8_UNORM:
	case Format::R8G8_UINT:
		return sizeof(std::uint8_t) * 2;
	case Format::R16_UNORM:
	case Format::R16_UINT:
	case Format::R16_SINT:
	case Format::R16_SFLOAT:
		return sizeof(std::uint16_t);
	case Format::R16G16_UNORM:
	case Format::R16G16_UINT:
	case Format::R16G16_SINT:
	case Format::R16G16_SFLOAT:
		return sizeof(std::uint16_t) * 2;
	case Format::R16G16B16_SFLOAT:
		return sizeof(std::uint16_t) * 3;
	case Format::R16G16B16A16_SFLOAT:
		return sizeof(std::uint16_t) * 4;
	case Format::R32_SFLOAT:
		return sizeof(float);
	case Format::R32G32_SFLOAT:
		return sizeof(float) * 2;
	case Format::R32G32B32_SFLOAT:
		return sizeof(float) * 3;
	case Format::R32G32B32A32_SFLOAT:
		return sizeof(float) * 4;
	default:
		return 0;
	}
}

} // namespace Backend

} // namespace Renderer

#endif // INCLUDED_RENDERER_BACKEND_FORMAT

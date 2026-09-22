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

#ifndef INCLUDED_CANVAS2D
#define INCLUDED_CANVAS2D

#include "graphics/Color.h"
#include "graphics/TextRenderer.h"
#include "graphics/Texture.h"
#include "maths/Rect.h"
#include "maths/Vector2D.h"

#include <cstdint>
#include <memory>
#include <vector>

class CRect;
class CTextRenderer;
class CVector2D;
namespace Renderer::Backend { class IDeviceCommandContext; }

// Encapsulates 2D drawing functionality to hide and optimize
// low level API calls.
class CCanvas2D
{
public:
	/**
	 * Drawing commands recorded on the logic thread and replayed on the render thread.
	 */
	struct DisplayList
	{
		struct Command
		{
			enum class Type
			{
				LINE,
				RECT,
				TEXTURE,
				TEXTURE_RELATIVE,
				ROTATED_TEXTURE,
				TEXT,
				PUSH_SCISSOR,
				POP_SCISSOR
			};
			Type type;
			std::vector<CVector2D> points;
			float width = 0.0f;
			CColor color;
			CTexturePtr texture;
			CRect destination;
			CRect source;
			CColor multiply;
			CColor add;
			float grayscaleFactor = 0.0f;
			CVector2D origin;
			float angle = 0.0f;
			CTextRenderer::Batches text;
		};
		std::vector<Command> commands;
	};

	CCanvas2D(
		const uint32_t widthInPixels, const uint32_t heightInPixels, const float scale,
		Renderer::Backend::IDeviceCommandContext* deviceCommandContext);

	/**
	 * Record into @p displayList instead of drawing.
	 */
	CCanvas2D(
		const uint32_t widthInPixels, const uint32_t heightInPixels, const float scale,
		DisplayList& displayList);
	~CCanvas2D();

	/**
	 * Draw the commands recorded in @p displayList.
	 */
	void Replay(const DisplayList& displayList);

	CCanvas2D(const CCanvas2D&) = delete;
	CCanvas2D& operator=(const CCanvas2D&) = delete;
	CCanvas2D(CCanvas2D&&) = delete;
	CCanvas2D& operator=(CCanvas2D&&) = delete;

	/**
	 * Draws a line by the given points with the width and color.
	 */
	void DrawLine(
		const std::vector<CVector2D>& points,
		const float width, const CColor& color);

	/**
	 * Draws the rect filled with the color.
	 */
	void DrawRect(const CRect& rect, const CColor& color);

	/**
	 * Draws a piece of the texture from the source rect into the
	 * destination rect. The result color is set by the following formula:
	 *   TEXTURE_COLOR * COLOR_MULTIPLY + COLOR_ADD
	 * The texture color is blended with its own grayscale version according to
	 * the grayscale factor.
	 */
	void DrawTexture(
		const CTexturePtr& texture, const CRect& destination, const CRect& source,
		const CColor& multiply, const CColor& add, const float grayscaleFactor);

	/**
	 * A simpler version of the previous one, draws the texture into the
	 * destination rect without color modifications.
	 */
	void DrawTexture(const CTexturePtr& texture, const CRect& destination);

	/**
	 * Draw a texture with @p source in fractions of its size, which is only
	 * known once it is uploaded.
	 */
	void DrawTextureRelative(
		const CTexturePtr& texture, const CRect& destination, const CRect& source,
		const CColor& multiply, const CColor& add, const float grayscaleFactor);

	/**
	 * A similar to the original one, draws the texture into the
	 * destination rect but rotates it first around the origin point by angle
	 * radians (a positive angle denotes a clockwise rotation).
	 */
	void DrawRotatedTexture(
		const CTexturePtr& texture, const CRect& destination, const CRect& source,
		const CColor& multiply, const CColor& add, const float grayscaleFactor,
		const CVector2D& origin, const float angle);

	/**
	 * Draws a text using canvas materials.
	 */
	void DrawText(CTextRenderer& textRenderer);

	/**
	 * Adds the scissor rect to a scissor stack. The only top scissor is
	 * applied. It's recommended to use as few nested scissors as possible.
	 */
	void PushScissor(const CRect& scissor);

	/**
	 * Removes the top scissor rect from a scissor stack. The stack must not be
	 * empty.
	 */
	void PopScissor();

	class ScopedScissor
	{
	public:
		ScopedScissor(CCanvas2D& canvas, const CRect& scissor)
			: m_Canvas(canvas)
		{
			m_Canvas.PushScissor(scissor);
		}

		~ScopedScissor()
		{
			m_Canvas.PopScissor();
		}

	private:
		CCanvas2D& m_Canvas;
	};

	/**
	 * Unbinds all bound resources and clears caches. Frequent calls might
	 * affect performance. Useful to call a custom rendering code.
	 */
	void Flush();

private:
	class Impl;
	std::unique_ptr<Impl> m;
};

#endif // INCLUDED_CANVAS2D

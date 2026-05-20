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

#ifndef STREAM_H
#define STREAM_H

#include <deque>
#include <optional>
#include <span>
#include <vector>

class CNetMessage;

class Stream
{
public:
	Stream(const std::int64_t streamId);

	std::int64_t GetId() const noexcept;

	void PushData(std::vector<std::uint8_t> data);
	void PushMessage(const CNetMessage* message);
	std::optional<std::span<const std::uint8_t>> PeekData();
	void MarkSent(const std::size_t offset);
	void MarkAcknowledged(const std::size_t offset);
	void Receive(const std::span<const std::uint8_t> data);
	std::vector<std::vector<std::uint8_t>> GetAllMessages();

private:
	std::int64_t m_Id;
	std::deque<std::vector<std::uint8_t>> m_SendBuffer;
	std::deque<std::uint8_t> m_ReceiveBuffer;

	// invariant: m_SentOffset >= m_AckedOffset
	std::size_t m_SentOffset{0};
	std::size_t m_AckedOffset{0};
};

#endif // STREAM_H

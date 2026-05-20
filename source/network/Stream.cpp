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

#include "Stream.h"

#include "network/NetHost.h"
#include "network/Serialization.h"

Stream::Stream(const std::int64_t streamId):
	m_Id{streamId}
{}

std::int64_t Stream::GetId() const noexcept
{
	return m_Id;
}

void Stream::PushData(std::vector<std::uint8_t> data)
{
	m_SendBuffer.push_back(std::move(data));
}

void Stream::PushMessage(const CNetMessage* message)
{
	m_SendBuffer.push_back(CNetHost::CreatePacket(message));
}

std::optional<std::span<const std::uint8_t>> Stream::PeekData()
{
	const std::size_t startOffset{m_SentOffset - m_AckedOffset};
	std::size_t offset{0};

	for (std::vector<std::uint8_t>& bytes : m_SendBuffer)
	{
		if (startOffset - offset < bytes.size())
		{
			const std::size_t temp{startOffset - offset};
			return std::span{bytes.data() + temp, bytes.size() - temp};
		}

		offset += bytes.size();
	}

	return std::nullopt;
}

void Stream::MarkSent(const std::size_t offset)
{
	m_SentOffset += offset;
}

void Stream::MarkAcknowledged(const std::size_t offset)
{
	while (!m_SendBuffer.empty())
	{
		std::vector<uint8_t>& head{m_SendBuffer.front()};
		if (m_AckedOffset + head.size() > offset)
			break;

		m_AckedOffset += head.size();
		m_SendBuffer.pop_front();
	}
}

void Stream::Receive(const std::span<const std::uint8_t> data)
{
	m_ReceiveBuffer.insert(m_ReceiveBuffer.end(), data.begin(), data.end());
}

namespace
{
std::optional<std::vector<std::uint8_t>> GetFirstMessage(std::deque<std::uint8_t>& buffer)
{
	if (buffer.size() < 3)
		return std::nullopt;

	auto bufferIter = buffer.begin();
	std::size_t messageSize;
	Deserialize_int_1(bufferIter, std::ignore);
	Deserialize_int_2(bufferIter, messageSize);
	if (messageSize > buffer.size())
		return std::nullopt;

	std::vector<std::uint8_t> messageCopy{buffer.begin(), buffer.begin() + messageSize};
	buffer.erase(buffer.begin(), buffer.begin() + messageSize);
	return messageCopy;
}
}

std::vector<std::vector<std::uint8_t>> Stream::GetAllMessages()
{
	std::vector<std::vector<std::uint8_t>> messages;
	while (auto message{GetFirstMessage(m_ReceiveBuffer)})
	{
		messages.push_back(std::move(message).value());
	}
	return messages;
}

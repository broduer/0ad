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

#include "HashSerializer.h"

#include "simulation2/serialization/BinarySerializer.h"

namespace Script { class Interface; }

CHashSerializer::CHashSerializer(const Script::Interface& scriptInterface) :
	CBinarySerializer<CHashSerializerImpl>(scriptInterface)
{
}

size_t CHashSerializer::GetHashLength()
{
	return m_Impl.GetHashLength();
}

const u8* CHashSerializer::ComputeHash()
{
	return m_Impl.ComputeHash();
}

size_t CHashSerializerImpl::GetHashLength()
{
	return HashFunc::DIGESTSIZE;
}

const u8* CHashSerializerImpl::ComputeHash()
{
	m_Hash.Final(m_HashData);
	return m_HashData;
}

void CHashSerializer::RNG(const std::mt19937_64& rng)
{
	// The serialization of the rng depends on the platform but should be compatible.
	// Check only whether the next two numbers are the same.
	std::mt19937_64 rngCopy{rng};
	NumberU32_Unbounded("rng0", rngCopy());
	NumberU32_Unbounded("rng1", rngCopy());
}

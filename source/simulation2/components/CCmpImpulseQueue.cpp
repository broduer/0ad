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

#include "simulation2/components/ICmpImpulseQueue.h"

#include "simulation2/components/ICmpPhysicalBody.h"
#include "simulation2/serialization/SerializeTemplates.h"
#include "simulation2/system/Component.h"

#include <algorithm>
#include <cstdint>
#include <vector>

class CCmpImpulseQueue final : public ICmpImpulseQueue
{
public:
    static void ClassInit(CComponentManager& componentManager);
    DEFAULT_COMPONENT_ALLOCATOR(ImpulseQueue)

    void Init(const CParamNode& paramNode) override;
    void Deinit() override;
    void HandleMessage(const CMessage& msg, bool global) override;

    void Serialize(ISerializer& serialize) override;
    void Deserialize(const CParamNode& paramNode, IDeserializer& deserialize) override;

    void AddImpulse(const ImpulseRequest& request) override;
    void AddImpulses(const std::vector<ImpulseRequest>& requests) override;
    void AddWeightDampedImpulse(entity_id_t target, entity_pos_t impulseX, entity_pos_t impulseY, uint8_t pressureIncrease) override;
    std::vector<ImpulseRequest> ConsumeImpulses() override;

    static std::string GetSchema() {
        return "<a:component type='system'/><empty/>";
    }

private:
    std::vector<ImpulseRequest> m_PendingImpulses;
};

void CCmpImpulseQueue::ClassInit(CComponentManager& /*componentManager*/)
{
}

void CCmpImpulseQueue::Init(const CParamNode& /*paramNode*/)
{
    // Nothing to initialize
}

void CCmpImpulseQueue::Deinit()
{
    m_PendingImpulses.clear();
}

void CCmpImpulseQueue::HandleMessage(const CMessage& msg, bool global)
{
    // No messages to handle yet
    (void)msg;
    (void)global;
}

void CCmpImpulseQueue::Serialize(ISerializer& serialize)
{
    // Serialize pending impulses to maintain consistency across saves
    std::uint32_t size = static_cast<std::uint32_t>(m_PendingImpulses.size());
    serialize.NumberU32_Unbounded("pending_size", size);

    for (std::uint32_t i = 0; i < size; ++i)
    {
        const ImpulseRequest& req = m_PendingImpulses[i];
        serialize.NumberU32_Unbounded("target", req.target);
        serialize.NumberFixed_Unbounded("force_x", req.force.X);
        serialize.NumberFixed_Unbounded("force_y", req.force.Y);
        serialize.NumberFixed_Unbounded("velocity_change_x", req.velocityChange.X);
        serialize.NumberFixed_Unbounded("velocity_change_y", req.velocityChange.Y);
        serialize.NumberU8_Unbounded("pressure", req.pressureIncrease);
        serialize.NumberFixed_Unbounded("angular_impulse", req.angularImpulse);
        serialize.NumberFixed_Unbounded("roll_impulse", req.rollImpulse);
    }
}

void CCmpImpulseQueue::Deserialize(const CParamNode& paramNode, IDeserializer& deserialize)
{
    Init(paramNode);

    std::uint32_t size = 0;
    deserialize.NumberU32_Unbounded("pending_size", size);

    m_PendingImpulses.clear();
    m_PendingImpulses.reserve(size);

    for (std::uint32_t i = 0; i < size; ++i)
    {
        ImpulseRequest req;
        deserialize.NumberU32_Unbounded("target", req.target);
        deserialize.NumberFixed_Unbounded("force_x", req.force.X);
        deserialize.NumberFixed_Unbounded("force_y", req.force.Y);
        deserialize.NumberFixed_Unbounded("velocity_change_x", req.velocityChange.X);
        deserialize.NumberFixed_Unbounded("velocity_change_y", req.velocityChange.Y);
        deserialize.NumberU8_Unbounded("pressure", req.pressureIncrease);
        deserialize.NumberFixed_Unbounded("angular_impulse", req.angularImpulse);
        deserialize.NumberFixed_Unbounded("roll_impulse", req.rollImpulse);
        m_PendingImpulses.push_back(req);
    }
}

void CCmpImpulseQueue::AddImpulse(const ImpulseRequest& request)
{
    m_PendingImpulses.push_back(request);
}

void CCmpImpulseQueue::AddImpulses(const std::vector<ImpulseRequest>& requests)
{
    m_PendingImpulses.insert(m_PendingImpulses.end(), requests.begin(), requests.end());
}

void CCmpImpulseQueue::AddWeightDampedImpulse(entity_id_t target, entity_pos_t impulseX, entity_pos_t impulseY, uint8_t pressureIncrease)
{
    ImpulseRequest request{target, CFixedVector2D{}, pressureIncrease};
    request.velocityChange = CFixedVector2D{impulseX, impulseY};

    // Heavier units get less knockback, and less pressure. Units lighter than 1 don't get more pressure than given.
    CmpPtr<ICmpPhysicalBody> cmpPhysicalBody(GetSimContext(), target);
    const entity_pos_t targetWeight{cmpPhysicalBody ? cmpPhysicalBody->GetWeight() : entity_pos_t::Zero()};
    if (targetWeight > entity_pos_t::Zero())
    {
        request.velocityChange = CFixedVector2D{impulseX / targetWeight, impulseY / targetWeight};
        if (targetWeight > fixed::FromInt(1))
            request.pressureIncrease = static_cast<uint8_t>((fixed::FromInt(pressureIncrease) / targetWeight).ToInt_RoundToZero());
    }

    AddImpulse(request);
}

std::vector<ImpulseRequest> CCmpImpulseQueue::ConsumeImpulses()
{
    std::vector<ImpulseRequest> result;
    result.swap(m_PendingImpulses);
    return result;
}

REGISTER_COMPONENT_TYPE(ImpulseQueue)

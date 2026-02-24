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

#include "ICmpCinemaManager.h"

#include "lib/debug.h"
#include "maths/Fixed.h"
#include "maths/FixedVector3D.h"
#include "maths/NUSpline.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/Game.h"
#include "renderer/Renderer.h"
#include "renderer/SceneRenderer.h"
#include "simulation2/MessageTypes.h"
#include "simulation2/components/ICmpRangeManager.h"
#include "simulation2/helpers/CinemaPath.h"
#include "simulation2/system/Component.h"
#include "simulation2/system/Entity.h"
#include "simulation2/system/Message.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

class CCmpCinemaManager final : public ICmpCinemaManager
{
public:
	static void ClassInit(CComponentManager& componentManager)
	{
		componentManager.SubscribeToMessageType(MT_Update);
		componentManager.SubscribeToMessageType(MT_Deserialized);
	}

	DEFAULT_COMPONENT_ALLOCATOR(CinemaManager)

	static std::string GetSchema()
	{
		return "<a:component type='system'/><empty/>";
	}

	void Init(const CParamNode&) override
	{
		m_IsPlayingPathQueue = false;
		m_QueuePlayingElapsedTime = fixed::Zero();
		m_PathQueueDuration = fixed::Zero();
		m_ActivePathElapsedTime = fixed::Zero();
		m_WasMapRevealed = false;
	}

	void Deinit() override
	{
	}

	void Serialize(ISerializer& serializer) override
	{
		serializer.Bool("IsPlayingPathQueue", m_IsPlayingPathQueue);
		serializer.NumberFixed_Unbounded("QueueElapsedTime", m_QueuePlayingElapsedTime);
		serializer.NumberFixed_Unbounded("CurrentPathElapsedTime", m_ActivePathElapsedTime);
		serializer.Bool("WasMapRevealed", m_WasMapRevealed);

		serializer.NumberU32_Unbounded("NumberOfPaths", m_Paths.size());
		for (const std::pair<const CStrW, CCinemaPath>& it : m_Paths)
			SerializePath(it.second, serializer);

		serializer.NumberU32_Unbounded("NumberOfQueuedPaths", m_PathQueue.size());
		for (const CCinemaPath& path : m_PathQueue)
			serializer.String("QueuedPathName", path.GetName(), 1, 128);
	}

	void Deserialize(const CParamNode&, IDeserializer& deserializer) override
	{
		deserializer.Bool("IsPlayingPathQueue", m_IsPlayingPathQueue);
		deserializer.NumberFixed_Unbounded("QueueElapsedTime", m_QueuePlayingElapsedTime);
		deserializer.NumberFixed_Unbounded("CurrentPathElapsedTime", m_ActivePathElapsedTime);
		deserializer.Bool("WasMapRevealed", m_WasMapRevealed);

		uint32_t numberOfPaths = 0;
		deserializer.NumberU32_Unbounded("NumberOfPaths", numberOfPaths);
		for (uint32_t i = 0; i < numberOfPaths; ++i)
		{
			CCinemaPath path = DeserializePath(deserializer);
			m_Paths[path.GetName()] = path;
		}

		uint32_t numberOfQueuedPaths = 0;
		deserializer.NumberU32_Unbounded("NumberOfQueuedPaths", numberOfQueuedPaths);
		for (uint32_t i = 0; i < numberOfQueuedPaths; ++i)
		{
			CStrW pathName;
			deserializer.String("QueuedPathName", pathName, 1, 128);
			ENSURE(HasPath(pathName));
			PushPathToQueue(pathName);
		}

		if (!m_PathQueue.empty())
		{
			m_PathQueue.front().m_TimeElapsed = m_ActivePathElapsedTime.ToFloat();
			m_PathQueue.front().Validate();
		}
	}

	void HandleMessage(const CMessage& msg, bool /*global*/) override
	{
		switch (msg.GetType())
		{
		case MT_Deserialized:
			if (!m_IsPlayingPathQueue)
				break;

			m_IsPlayingPathQueue = false;
			StartPlayingQueue();
			break;
		case MT_Update:
		{
			const CMessageUpdate &msgData = static_cast<const CMessageUpdate&>(msg);
			if (!m_IsPlayingPathQueue)
				break;

			// The paths play at a fixed speed, no matter the sim rate.
			// The turn length we have received here, however, is scaled by that rate.
			const fixed realTurnLength{msgData.turnLength / fixed::FromFloat(g_Game ? g_Game->GetSimRate() : 1.0f)};
			m_QueuePlayingElapsedTime += realTurnLength;
			m_ActivePathElapsedTime += realTurnLength;
			if (m_ActivePathElapsedTime >= m_PathQueue.front().GetDuration())
			{
				CMessageCinemaPathEnded msgCinemaPathEnded(m_PathQueue.front().GetName());
				m_PathQueue.pop_front();
				GetSimContext().GetComponentManager().PostMessage(SYSTEM_ENTITY, msgCinemaPathEnded);
				m_ActivePathElapsedTime = fixed::Zero();

				if (!m_PathQueue.empty())
					m_PathQueue.front().Reset();
			}

			if (m_QueuePlayingElapsedTime >= m_PathQueueDuration)
				StopPlayingQueue();
			break;
		}
		default:
			break;
		}
	}

	void AddPath(const CCinemaPath& path) override
	{
		if (m_Paths.find(path.GetName()) != m_Paths.end())
		{
			LOGWARNING("Cinema path with name '%s' already exists", path.GetName().ToUTF8());
			return;
		}
		m_Paths[path.GetName()] = path;
	}

	void DeletePath(const CStrW& name) override
	{
		if (!HasPath(name))
		{
			LOGWARNING("Cinema path with name '%s' doesn't exist", name.ToUTF8());
			return;
		}
		m_PathQueue.remove_if([name](const CCinemaPath& path) { return path.GetName() == name; });
		m_Paths.erase(name);
	}

	bool HasPath(const CStrW& name) const override
	{
		return m_Paths.find(name) != m_Paths.end();
	}

	const std::map<CStrW, CCinemaPath>& GetPaths() const override
	{
		return m_Paths;
	}

	void SetPaths(const std::map<CStrW, CCinemaPath>& newPaths) override
	{
		m_Paths = newPaths;
	}

	void PushPathToQueue(const CStrW& name) override
	{
		if (!HasPath(name))
		{
			LOGWARNING("Cinema path with name '%s' doesn't exist", name.ToUTF8());
			return;
		}
		m_PathQueue.push_back(m_Paths[name]);

		if (m_PathQueue.size() == 1)
			m_PathQueue.front().Reset();
		m_PathQueueDuration += m_Paths[name].GetDuration();
	}

	void ClearQueue() override
	{
		m_PathQueue.clear();
	}

	void StartPlayingQueue() override
	{
		if (m_IsPlayingPathQueue || m_PathQueue.empty())
			return;

		CmpPtr<ICmpRangeManager> cmpRangeManager(GetSimContext().GetSystemEntity());
		if (cmpRangeManager)
		{
			m_WasMapRevealed = cmpRangeManager->GetLosRevealWholeMapForAll();
			// Note: this results in all fogged entities seen during the cinema path being revealed/updated in FOW
			// after the queue has ended.
			cmpRangeManager->SetLosRevealWholeMapForAll(true);
		}

		m_IsPlayingPathQueue = true;
	}

	void StopPlayingQueue() override
	{
		if (!m_IsPlayingPathQueue)
			return;

		CmpPtr<ICmpRangeManager> cmpRangeManager(GetSimContext().GetSystemEntity());
		if (cmpRangeManager)
			cmpRangeManager->SetLosRevealWholeMapForAll(m_WasMapRevealed);

		GetSimContext().GetComponentManager().PostMessage(SYSTEM_ENTITY, CMessageCinemaQueueEnded());

		m_ActivePathElapsedTime = fixed::Zero();
		m_QueuePlayingElapsedTime = fixed::Zero();
		m_PathQueueDuration = fixed::Zero();
		for (const CCinemaPath& path : m_PathQueue)
			m_PathQueueDuration += path.GetDuration();
		m_IsPlayingPathQueue = false;
	}

	bool IsPlayingQueue() const override
	{
		return m_IsPlayingPathQueue;
	}

	void UpdateActivePath(const float deltaRealTime, CCamera* camera) override
	{
		if (m_IsPlayingPathQueue)
		{
			if (m_PathQueue.empty())
				StopPlayingQueue();
			else
				m_PathQueue.front().Play(deltaRealTime, camera);
		}
	}

	const CStrW GetActivePath() const override
	{
		return m_IsPlayingPathQueue ? m_PathQueue.front().GetName() : CStrW();
	}

	const fixed GetActivePathElapsedTime() const override
	{
		return m_ActivePathElapsedTime;
	}

private:

	void SerializePath(const CCinemaPath& path, ISerializer& serializer)
	{
		const CCinemaData* data = path.GetData();

		serializer.String("PathName", data->m_Name, 1, 128);
		serializer.String("PathOrientation", data->m_Orientation, 1, 128);
		serializer.String("PathMode", data->m_Mode, 1, 128);
		serializer.String("PathStyle", data->m_Style, 1, 128);
		serializer.NumberFixed_Unbounded("PathTimescale", data->m_Timescale);
		serializer.Bool("LookAtTarget", data->m_LookAtTarget);

		serializer.NumberU32("NumberOfNodes", path.GetAllNodes().size(), 1, MAX_SPLINE_NODES);
		const std::vector<SplineData>& nodes = path.GetAllNodes();
		for (size_t i = 0; i < nodes.size(); ++i)
		{
			if (i > 0)
				serializer.NumberFixed_Unbounded("NodeDeltaTime", nodes[i - 1].Distance);
			else
				serializer.NumberFixed_Unbounded("NodeDeltaTime", fixed::Zero());

			serializer.NumberFixed_Unbounded("PositionX", nodes[i].Position.X);
			serializer.NumberFixed_Unbounded("PositionY", nodes[i].Position.Y);
			serializer.NumberFixed_Unbounded("PositionZ", nodes[i].Position.Z);

			serializer.NumberFixed_Unbounded("RotationX", nodes[i].Rotation.X);
			serializer.NumberFixed_Unbounded("RotationY", nodes[i].Rotation.Y);
			serializer.NumberFixed_Unbounded("RotationZ", nodes[i].Rotation.Z);
		}

		if (!data->m_LookAtTarget)
			return;

		const std::vector<SplineData>& targetNodes = path.GetTargetSpline().GetAllNodes();
		serializer.NumberU32("NumberOfTargetNodes", targetNodes.size(), 1, MAX_SPLINE_NODES);
		for (size_t i = 0; i < targetNodes.size(); ++i)
		{
			if (i > 0)
				serializer.NumberFixed_Unbounded("NodeDeltaTime", targetNodes[i - 1].Distance);
			else
				serializer.NumberFixed_Unbounded("NodeDeltaTime", fixed::Zero());
			serializer.NumberFixed_Unbounded("PositionX", targetNodes[i].Position.X);
			serializer.NumberFixed_Unbounded("PositionY", targetNodes[i].Position.Y);
			serializer.NumberFixed_Unbounded("PositionZ", targetNodes[i].Position.Z);
		}
	}

	CCinemaPath DeserializePath(IDeserializer& deserializer)
	{
		CCinemaData data;

		deserializer.String("PathName", data.m_Name, 1, 128);
		deserializer.String("PathOrientation", data.m_Orientation, 1, 128);
		deserializer.String("PathMode", data.m_Mode, 1, 128);
		deserializer.String("PathStyle", data.m_Style, 1, 128);
		deserializer.NumberFixed_Unbounded("PathTimescale", data.m_Timescale);
		deserializer.Bool("LookAtTarget", data.m_LookAtTarget);

		TNSpline pathSpline, targetSpline;
		uint32_t numberOfNodes = 0;
		deserializer.NumberU32("NumberOfNodes", numberOfNodes, 1, MAX_SPLINE_NODES);
		for (uint32_t j = 0; j < numberOfNodes; ++j)
		{
			SplineData node;
			deserializer.NumberFixed_Unbounded("NodeDeltaTime", node.Distance);

			deserializer.NumberFixed_Unbounded("PositionX", node.Position.X);
			deserializer.NumberFixed_Unbounded("PositionY", node.Position.Y);
			deserializer.NumberFixed_Unbounded("PositionZ", node.Position.Z);

			deserializer.NumberFixed_Unbounded("RotationX", node.Rotation.X);
			deserializer.NumberFixed_Unbounded("RotationY", node.Rotation.Y);
			deserializer.NumberFixed_Unbounded("RotationZ", node.Rotation.Z);

			pathSpline.AddNode(node.Position, node.Rotation, node.Distance);
		}

		if (data.m_LookAtTarget)
		{
			uint32_t numberOfTargetNodes = 0;
			deserializer.NumberU32("NumberOfTargetNodes", numberOfTargetNodes, 1, MAX_SPLINE_NODES);
			for (uint32_t j = 0; j < numberOfTargetNodes; ++j)
			{
				SplineData node;
				deserializer.NumberFixed_Unbounded("NodeDeltaTime", node.Distance);

				deserializer.NumberFixed_Unbounded("PositionX", node.Position.X);
				deserializer.NumberFixed_Unbounded("PositionY", node.Position.Y);
				deserializer.NumberFixed_Unbounded("PositionZ", node.Position.Z);

				targetSpline.AddNode(node.Position, CFixedVector3D(), node.Distance);
			}
		}

		return CCinemaPath(data, pathSpline, targetSpline);
	}

	bool m_IsPlayingPathQueue;
	std::map<CStrW, CCinemaPath> m_Paths;
	std::list<CCinemaPath> m_PathQueue;
	fixed m_PathQueueDuration;

	// Total time elapsed since starting to play the queue.
	fixed m_QueuePlayingElapsedTime;

	// Time elapsed since the currently active path first started playing.
	fixed m_ActivePathElapsedTime;

	// Time elapsed since the
	fixed m_QueueEndedElapsedTime;

	// Whether the map was revealed before playing the queue.
	bool m_WasMapRevealed;
};

REGISTER_COMPONENT_TYPE(CinemaManager)

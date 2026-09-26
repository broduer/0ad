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

#include "Unit.h"

#include "graphics/Model.h"
#include "graphics/ModelAbstract.h"
#include "graphics/ObjectBase.h"
#include "graphics/ObjectEntry.h"
#include "graphics/ObjectManager.h"
#include "graphics/UnitAnimation.h"
#include "lib/code_generation.h"
#include "ps/CLogger.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

CUnit::CUnit(CObjectManager& objectManager, const CActorDef& actor, const entity_id_t id, const uint32_t seed,
	const uint32_t randomSeed)
	: m_ObjectManager(objectManager), m_Actor(actor), m_ID(id), m_Seed(seed), m_RandomSeed(randomSeed),
	m_Animation(nullptr)
{
	/**
	 * When entity selections change, we might end up with a different layout in terms of variants/groups,
	 * which means the random key calculation might end up with different results for the same seed.
	 * This is bad, as it means entities randomly change appearence when changing e.g. animation.
	 * To fix this, we'll initially pick a random and complete specification based on our seed,
	 * and then pass that as the lowest priority selections. Thus, if the actor files are properly specified,
	 * we can ensure that the entities will look the same no matter what happens.
	 */
	SetActorSelections(m_Actor.PickSelectionsAtRandom(m_Seed)); // Calls ReloadObject().
}

CUnit::~CUnit()
{
	delete m_Animation;
}

std::unique_ptr<CUnit> CUnit::Create(const CStrW& actorName, const entity_id_t id, const uint32_t seed,
	const uint32_t randomSeed, CObjectManager& objectManager)
{
	const CActorDef& actor{std::get<1>(objectManager.FindActorDef(actorName))};

	std::unique_ptr<CUnit> unit{new CUnit(objectManager, actor, id, seed, randomSeed)};
	if (!unit->m_Model)
		return nullptr;

	return unit;
}

void CUnit::UpdateModel(float frameTime)
{
	if (m_Animation)
		m_Animation->Update(frameTime*1000.0f);
}

void CUnit::SetEntitySelection(const CStr& key, const CStr& selection)
{
	CStr selection_lc = selection.LowerCase();

	if (m_EntitySelections[key] == selection_lc)
		return;
	m_EntitySelections[key] = selection_lc;

	ReloadObject();
}

void CUnit::SetEntitySelection(const std::map<CStr, CStr>& selections)
{
	for (const std::pair<const CStr, CStr>& s : selections)
		m_EntitySelections[s.first] = s.second.LowerCase();

	ReloadObject();
}

void CUnit::SetActorSelections(const std::set<CStr>& selections)
{
	m_ActorSelections = selections;
	m_Variations.clear();
	ReloadObject();
}

CObjectEntry* CUnit::FindObjectVariation() const
{
	std::set<CStr> entitySelections;
	for (const std::pair<const CStr, CStr>& selection : m_EntitySelections)
		entitySelections.insert(selection.second);
	std::vector<std::set<CStr>> selections;
	selections.push_back(entitySelections);
	selections.push_back(m_ActorSelections);

	// randomly select any remain selections necessary to completely identify a variation (e.g., the new selection
	// made might define some additional props that require a random variant choice). Also, FindObjectVariation
	// expects the selectors passed to it to be complete.
	// see https://gitea.wildfiregames.com/0ad/0ad/issues/979

	// If these selections give a different object, change this unit to use it
	// Use the entity ID as randomization seed (same as when the unit was first created)
	return m_ObjectManager.FindObjectVariation(&m_Actor, selections, m_Seed);
}

void CUnit::ReloadObject()
{
	const auto cached = std::ranges::find(m_Variations, m_EntitySelections,
		&std::pair<std::map<CStr, CStr>, CObjectEntry*>::first);
	CObjectEntry* newObject{cached != m_Variations.end() ? cached->second : FindObjectVariation()};
	if (!newObject)
	{
		LOGERROR("Error loading object variation (actor: %s)", m_Actor.GetPathname().string8());
		// Don't delete the unit, don't override our current (valid) state.
		return;
	}
	if (m_Object && cached == m_Variations.end())
	{
		if (m_Variations.size() == MAX_CACHED_VARIATIONS)
			m_Variations.erase(m_Variations.begin());
		m_Variations.emplace_back(m_EntitySelections, newObject);
	}

	if (!m_Object)
	{
		m_Object = newObject;
		m_Model = newObject->m_Model->Clone();
		m_Model->SetRandomSeed(m_RandomSeed);
		if (m_Model->ToCModel())
			m_Animation = new CUnitAnimation(m_ID, m_Model->ToCModel(), m_Object, m_RandomSeed);
	}
	else if (m_Object && newObject != m_Object)
	{
		// Clone the new object's base (non-instance) model
		std::unique_ptr<CModelAbstract> newModel = newObject->m_Model->Clone();
		newModel->SetRandomSeed(m_RandomSeed);

		// Copy the old instance-specific settings from the old model to the new instance
		newModel->SetTransform(m_Model->GetTransform());
		newModel->SetPlayerID(m_Model->GetPlayerID());
		if (newModel->ToCModel() && m_Model->ToCModel())
		{
			newModel->ToCModel()->CopyAnimationFrom(m_Model->ToCModel());

			// Copy flags that belong to this model instance (not those defined by the actor XML)
			int instanceFlags = (ModelFlag::SILHOUETTE_DISPLAY | ModelFlag::SILHOUETTE_OCCLUDER | ModelFlag::IGNORE_LOS) & m_Model->ToCModel()->GetFlags();
			newModel->ToCModel()->AddFlagsRec(instanceFlags);
		}

		m_Model = std::move(newModel);
		m_Object = newObject;

		if (m_Model->ToCModel())
		{
			if (m_Animation)
				m_Animation->ReloadUnit(m_Model->ToCModel(), m_Object); // TODO: maybe this should try to preserve animation state?
			else
				m_Animation = new CUnitAnimation(m_ID, m_Model->ToCModel(), m_Object, m_RandomSeed);
		}
		else
		{
			SAFE_DELETE(m_Animation);
		}
	}
}

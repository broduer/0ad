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

#ifndef INCLUDED_OBJECTMANAGER
#define INCLUDED_OBJECTMANAGER

#include "lib/code_annotation.h"
#include "lib/file/vfs/vfs_path.h"
#include "lib/status.h"
#include "ps/CStr.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <utility>
#include <vector>

class CActorDef;
class CRenderingOptions;
class CMeshManager;
class CObjectBase;
class CObjectEntry;
class CSkeletonAnimManager;
class IGroundView;
class CTerrain;

///////////////////////////////////////////////////////////////////////////////////////////
// CObjectManager: manager class for all possible actor types
class CObjectManager
{
	NONCOPYABLE(CObjectManager);
public:
	// Unique identifier of an actor variation
	struct ObjectKey
	{
		ObjectKey(const CStr& identifier, const std::vector<std::uint8_t>& var)
			: ObjectBaseIdentifier(identifier), ActorVariation(var) {}

		bool operator< (const CObjectManager::ObjectKey& a) const;

	private:
		CStr ObjectBaseIdentifier;
		std::vector<std::uint8_t> ActorVariation;
	};

	/**
	 * Governs how random variants are selected by ObjectBase
	 */
	enum class VariantDiversity
	{
		NONE,
		LIMITED,
		FULL
	};

public:

	// constructor, destructor
	CObjectManager(CMeshManager& meshManager, CSkeletonAnimManager& skeletonAnimManager);
	~CObjectManager();

	// Provide access to the manager classes for meshes and animations - they're
	// needed when objects are being created and so this seems like a convenient
	// place to centralise access.
	CMeshManager& GetMeshManager() const { return m_MeshManager; }
	CSkeletonAnimManager& GetSkeletonAnimManager() const { return m_SkeletonAnimManager; }

	void UnloadObjects();

	/**
	 * Get the actor definition for the given path name.
	 * If the actor cannot be loaded, this will return a placeholder actor.
	 * @return Success/failure boolean and a valid actor definition.
	 */
	std::pair<bool, CActorDef&> FindActorDef(const CStrW& actorName);

	/**
	 * Get the object entry for a given actor & the given selections list.
	 * @param selections - a possibly incomplete list of selections.
	 * @param seed - the randomness seed to use to complete the random selections.
	 */
	CObjectEntry* FindObjectVariation(const CActorDef* actor, const std::vector<std::set<CStr>>& selections, uint32_t seed);

	/**
	 * @see FindObjectVariation.
	 * These take a complete selection. These are pointers to sets that are
	 * guaranteed to exist (pointers are used to avoid copying the sets).
	 */
	CObjectEntry* FindObjectVariation(const std::shared_ptr<CObjectBase>& base, const std::vector<const std::set<CStr>*>& completeSelections);
	CObjectEntry* FindObjectVariation(const CStrW& objname, const std::vector<const std::set<CStr>*>& completeSelections);

	/**
	 * Set where the models of new objects find the ground and water, before
	 * the first object is created.
	 */
	void SetGround(const IGroundView& ground) { m_Ground = &ground; }

	VariantDiversity GetVariantDiversity() const;

	/**
	 * Reload any scripts that were loaded from the given filename.
	 * (This is used to implement hotloading.)
	 */
	Status ReloadChangedFile(const VfsPath& path);

	/**
	 * Set the callback that rebuilds the units of an actor (after a hotload),
	 * or all units for an empty name (after a quality or diversity change).
	 */
	void SetActorsChangedCallback(std::function<void(const std::wstring&)> callback)
	{
		m_ActorsChanged = std::move(callback);
	}

	/**
	 * Follow the actor quality and variant diversity options, rebuilding the
	 * units through the actors-changed callback when they change.
	 */
	void UpdateOptions(const CRenderingOptions& options);

	CMeshManager& m_MeshManager;
	CSkeletonAnimManager& m_SkeletonAnimManager;
	const IGroundView* m_Ground{nullptr};
	std::function<void(const std::wstring&)> m_ActorsChanged;

	std::uint8_t m_QualityLevel = 100;
	VariantDiversity m_VariantDiversity = VariantDiversity::FULL;

	template<typename T>
	struct Hotloadable
	{
		Hotloadable() = default;
		Hotloadable(std::unique_ptr<T>&& ptr) : obj(std::move(ptr)) {}
		bool outdated = false;
		std::unique_ptr<T> obj;
	};
	// TODO: define a hash and switch to unordered_map
	std::map<ObjectKey, Hotloadable<CObjectEntry>> m_Objects;
	std::unordered_map<CStrW, Hotloadable<CActorDef>> m_ActorDefs;
};

#endif

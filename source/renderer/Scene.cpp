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

#include "Scene.h"

#include "graphics/Model.h"
#include "graphics/ModelAbstract.h"
#include "graphics/ParticleEmitter.h"
#include "lib/debug.h"

#include <cstddef>
#include <memory>
#include <vector>

void SceneCollector::SubmitNode(CModelAbstract* model)
{
	if (model->ToCModel())
		SubmitNonRecursive(model->ToCModel());
	else if (model->ToCModelDecal())
		Submit(model->ToCModelDecal());
	else if (model->ToCModelParticleEmitter())
		Submit(model->ToCModelParticleEmitter()->m_Emitter.get());
	else if (model->ToCModelDummy())
	{
	}
	else
		debug_warn(L"unknown model type");
}

///////////////////////////////////////////////////////////
// Default implementation traverses the model recursively and uses
// SubmitNode for the actual work.
void SceneCollector::SubmitRecursive(CModelAbstract* model)
{
	SubmitNode(model);

	if (CModel* asModel = model->ToCModel())
	{
		const std::vector<CModel::Prop>& props = asModel->GetProps();
		for (size_t i = 0; i < props.size(); i++)
		{
			if (!props[i].m_Hidden)
				SubmitRecursive(props[i].m_Model.get());
		}
	}
}

void SceneCollector::Flatten(CModelAbstract* model, std::vector<CModelAbstract*>& out)
{
	out.push_back(model);

	if (CModel* asModel = model->ToCModel())
	{
		const std::vector<CModel::Prop>& props = asModel->GetProps();
		for (size_t i = 0; i < props.size(); i++)
		{
			if (!props[i].m_Hidden)
				Flatten(props[i].m_Model.get(), out);
		}
	}
}

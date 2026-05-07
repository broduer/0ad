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

#include "AtlasObject.h"
#include "AtlasObjectImpl.h"

#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

static AtSmartPtr<AtNode> ConvertNode(json node);

AtObj AtlasObject::LoadFromJSON(const std::string& json)
{
	auto rootnode = json::parse(json);

	AtObj obj;
	obj.m_Node = ConvertNode(rootnode);
	return obj;
}

// Convert from a JSON to an AtNode
static AtSmartPtr<AtNode> ConvertNode(json node)
{
	AtSmartPtr<AtNode> obj (new AtNode());

	if (node.is_string())
	{
		obj->m_Value = node.dump();
	}
	else if (node.is_number_integer() || node.is_number_unsigned())
	{
		std::stringstream stream;
		if (node.is_number_integer())
			stream << node.get<int>();
		if (node.is_number_unsigned())
			stream << node.get<unsigned int>();

		obj->m_Value = stream.str().c_str();
		obj->m_Children.insert(AtNode::child_pairtype(
			"@number", AtSmartPtr<AtNode>(new AtNode())
		));
	}
	else if (node.is_boolean())
	{
		obj->m_Value = node.get<bool>() ? "true" : "false";

		obj->m_Children.insert(AtNode::child_pairtype(
			"@boolean", AtSmartPtr<AtNode>(new AtNode())
		));
	}
	else if (node.is_array())
	{
		obj->m_Children.insert(AtNode::child_pairtype(
			"@array", AtSmartPtr<AtNode>(new AtNode())
		));

		json nodeChildren = node.get<std::vector<json>>();

		for (auto child: nodeChildren)
		{
			obj->m_Children.insert(AtNode::child_pairtype(
				"item", ConvertNode(child)));
		}
	}
	else if (node.is_object())
	{
		json objectProperties = node;
		for (auto& property: objectProperties.items())
		{
			obj->m_Children.insert(AtNode::child_pairtype(
				property.key(), ConvertNode(property.value())
			));
		}
	}
	else if (node.is_null())
	{
		return obj;
	}
	else
	{
		assert(! "Unimplemented type found when parsing JSON!");
	}

	return obj;
}


static json BuildJSONNode(AtNode::Ptr p)
{
	if (!p)
	{
		json rval;
		return rval;
	}

	// Special case for numbers/booleans to allow round-tripping
	if (p->m_Children.count("@number"))
	{
		// Convert to double
		double val = 0;
		static_cast<std::stringstream>(p->m_Value) >> val;

		json rval(val);
		return rval;
	}
	else if (p->m_Children.count("@boolean"))
	{
		bool val = false;
		if (p->m_Value == "true")
			val = true;

		json rval(val);
		return rval;
	}

	// If no children, then use the value string instead
	if (p->m_Children.empty())
	{
		json rval(p->m_Value);
		return rval;
	}

	if (p->m_Children.find("@array") != p->m_Children.end())
	{
		auto rval = json::array();

		// Find the <item> children
		AtNode::child_maptype::const_iterator lower = p->m_Children.lower_bound("item");
		AtNode::child_maptype::const_iterator upper = p->m_Children.upper_bound("item");

		for (AtNode::child_maptype::const_iterator it = lower; it != upper; ++it)
		{
			json child = BuildJSONNode(it->second);
			rval.push_back(child);
		}

		return rval;
	}
	else
	{
		auto rval = json::object();

		for (AtNode::child_maptype::const_iterator it = p->m_Children.begin(); it != p->m_Children.end(); ++it)
		{
			json child = BuildJSONNode(it->second);
			// We don't serialize childs with null value.
			// Instead of something like this we omit the whole property: "StartingCamera": null
			// There's no special reason for that, it's just the same behaviour the previous implementations had.
			if (!child.is_null())
				rval.emplace(it->first.c_str(), child);
		}

		return rval;
	}
}

std::string AtlasObject::SaveToJSON(AtObj& obj)
{
	auto root = BuildJSONNode(obj.m_Node);
	return root.dump();
}

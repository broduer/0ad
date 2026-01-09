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

#include "XmppExternalServiceDiscovery.h"

// #include "lib/debug.h"
#include <cstdio>
#include <stdexcept>

namespace PS::Xmpp::ExternalServiceDiscovery
{

Manager::Manager(gloox::ClientBase* client)
	: m_Client(client)
{
	if (!m_Client)
		return;

	m_Client->registerConnectionListener(this);
	m_Client->registerStanzaExtension(new ExternalServicesExt());
	m_Client->registerIqHandler(this, EXTEXTERNALSERVICEDISCOVERY);
}

Manager::~Manager()
{
	if (!m_Client)
		return;

	m_Client->removeConnectionListener(this);
	m_Client->removeIqHandler(this, EXTEXTERNALSERVICEDISCOVERY);
	m_Client->removeStanzaExtension(EXTEXTERNALSERVICEDISCOVERY);
	m_Client->removeIDHandler(this);
}

bool Manager::handleIq(const gloox::IQ& iq)
{
	switch(iq.subtype())
	{
	case gloox::IQ::Get:
		break;
	case gloox::IQ::Set:
		break;
	case gloox::IQ::Result:
	{
		const ExternalServicesExt* es = iq.findExtension<ExternalServicesExt>(EXTEXTERNALSERVICEDISCOVERY);
		if (es)
		{
			m_Services.clear();
			for (const gloox::Tag* const& t : es->m_ServiceList)
			{
				std::printf("Service: '%s'\n", t->xml().c_str());
				Service service = Service();
				// required fields
				service.m_Type = t->findAttribute("type");
				service.m_Host = t->findAttribute("host");
				// optional fields
				try
				{
					service.m_Port = std::stoi(t->findAttribute("port"));
				}
				catch (const std::invalid_argument&)
				{
					service.m_Port = 0;
				}
				service.m_Transport = t->findAttribute("transport");
				service.m_Username = t->findAttribute("username");
				service.m_Password = t->findAttribute("password");
				service.m_Expires = t->findAttribute("expires");
				if (t->findAttribute("restricted") == "true")
					service.m_Restricted = true;
				m_Services.push_back(service);
			}
		}
		break;
	}
	case gloox::IQ::Error:
		break;
	case gloox::IQ::Invalid:
		break;
	}
	return true;
}

// Request STUN services on connect.
void Manager::onConnect()
{
	ExternalServicesExt* e = new ExternalServicesExt();
	e->m_Type = "stun";
	gloox::IQ iq(gloox::IQ::Get, gloox::JID(m_Client->server()) , m_Client->getID());
	iq.addExtension(e);
	m_Client->send(iq);
}

std::optional<Manager::Service> Manager::GetStunServer()
{
	for (auto service : m_Services)
	{
		if (service.m_Type == "stun" && service.m_Transport == "udp" && service.m_Restricted == false)
			return service;
	}
	return {};
}

ExternalServicesExt::ExternalServicesExt(const gloox::Tag* tag)
	: gloox::StanzaExtension(EXTEXTERNALSERVICEDISCOVERY)
{
	if (!tag || tag->name() != "services" || tag->xmlns() != XMLNS_EXTERNALSERVICEDISCOVERY)
		return;

	for (const gloox::Tag* const& t : tag->findTagList("services/service"))
		m_ServiceList.emplace_back(t->clone());
}

ExternalServicesExt::~ExternalServicesExt()
{
	for (const gloox::Tag* const & t : m_ServiceList)
		delete t;
	m_ServiceList.clear();
}

const std::string& ExternalServicesExt::filterString() const
{
	static const std::string filter = "/iq/services[@xmlns='" XMLNS_EXTERNALSERVICEDISCOVERY "']";
    return filter;
}

gloox::Tag* ExternalServicesExt::tag() const
{
	gloox::Tag* esd = new gloox::Tag("services");
	esd->setXmlns(XMLNS_EXTERNALSERVICEDISCOVERY);
	if (!m_Type.empty())
		esd->addAttribute("type", m_Type);

	for (const gloox::Tag* const& tag : m_ServiceList)
		esd->addChild(tag->clone());

	return esd;
}

gloox::StanzaExtension* ExternalServicesExt::clone() const
{
	return new ExternalServicesExt();
}

} // namespace PS::Xmpp::ExternalServiceDiscovery

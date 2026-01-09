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

#ifndef INCLUDED_EXTERNALSERVICEDISCOVERY
#define INCLUDED_EXTERNALSERVICEDISCOVERY

#include "lib/external_libraries/gloox.h"

#include <optional>
#include <string>
#include <vector>

#define EXTEXTERNALSERVICEDISCOVERY 1215
#define XMLNS_EXTERNALSERVICEDISCOVERY "urn:xmpp:extdisco:2"

/*
 * @file XEP-0215: External Service Discovery
 *
 * Incomplete implementation of XMPP extension protocol XEP-0215 covering the
 * bits needed for pyrogenesis as a gloox style manager.
 *
 * https://xmpp.org/extensions/xep-0215.html
 */
namespace PS::Xmpp::ExternalServiceDiscovery
{

/*
 * @class Manager
 *
 * Queries for external services announced by the XMPP server and offers access
 * to that information.
 */
class Manager : public gloox::IqHandler, public gloox::ConnectionListener
{
public:
	/**
	 * An external service as in XEP-215
	 */
	struct Service
	{
		std::string m_Host;
		std::string m_Type;
		std::string m_Transport;
		std::string m_Username;
		std::string m_Password;
		std::string m_Expires; // a date
		int m_Port;
		bool m_Restricted{false};
	};

	/*
	 * Constructor
	 *
	 * @param client The XMPP client to use for communication with the XMPP server.
	 */
	Manager(gloox::ClientBase* client);
	Manager(const Manager&) = delete;
	Manager& operator=(const Manager&) = delete;
	Manager(Manager&&) = delete;
	Manager& operator=(Manager&&) = delete;
	~Manager();

	/*
	 * Get the first unrestricted STUN server using udp
	 *
	 * @returns A Service if available or std::nullopt
	 *
	 * @todo very much not generic
	 */
	std::optional<Service> GetStunServer();

	// ConnectionListener
	void onConnect() override;
	void onDisconnect(gloox::ConnectionError /*error*/) override {}
	bool onTLSConnect(const gloox::CertInfo& /*info*/) override { return true; }

	// IqHandler
	bool handleIq(const gloox::IQ& iq) override;
	void handleIqID(const gloox::IQ& /*iq*/, int /*context*/) override {}

private:
	std::vector<Service> m_Services;
	gloox::ClientBase* m_Client;
};

class ExternalServicesExt : public gloox::StanzaExtension
{
public:
	ExternalServicesExt(const gloox::Tag* tag = nullptr);
	ExternalServicesExt(const ExternalServicesExt&) = delete;
	ExternalServicesExt& operator=(const ExternalServicesExt&) = delete;
	ExternalServicesExt(ExternalServicesExt&&) = delete;
	ExternalServicesExt& operator=(ExternalServicesExt&&) = delete;
	~ExternalServicesExt();

	gloox::StanzaExtension* newInstance(const gloox::Tag* tag) const override
	{
		return new ExternalServicesExt(tag);
	}
	const std::string& filterString() const override;
	gloox::Tag* tag() const override;
	gloox::StanzaExtension* clone() const override;

	std::vector<gloox::Tag*> m_ServiceList;
	std::string m_Type;
};

} // namespace PS::Xmpp::ExternalServiceDiscovery

#endif // INCLUDED_EXTERNALSERVICEDISCOVERY

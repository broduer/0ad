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

#include "NetServer.h"

#include "i18n/L10n.h"
#include "lib/code_generation.h"
#include "lib/debug.h"
#include "lib/hash.h"
#include "lib/secure_crt.h"
#include "lib/status.h"
#include "lib/utf8.h"
#include "network/FSM.h"
#include "network/NetFileTransfer.h"
#include "network/NetHost.h"
#include "network/NetMessage.h"
#include "network/NetProtocol.h"
#include "network/NetServerTurnManager.h"
#include "network/NetServerSession.h"
#include "network/NetStats.h"
#include "network/StunClient.h"
#include "ps/CLogger.h"
#include "ps/ConfigDB.h"
#include "ps/GUID.h"
#include "ps/GameSetup/CmdLineArgs.h"
#include "ps/GameSetup/Paths.h"
#include "ps/Hashing.h"
#include "ps/ProfileViewer.h"
#include "ps/Util.h"
#include "ps/Profiler2.h"
#include "ps/Threading.h"
#include "ps/algorithm.h"
#include "scriptinterface/JSON.h"
#include "scriptinterface/Object.h"
#include "scriptinterface/Context.h"
#include "scriptinterface/Interface.h"
#include "scriptinterface/Request.h"
#include "simulation2/helpers/SimulationCommand.h"
#include "simulation2/system/TurnManager.h"

#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <fmt/format.h>
#include <functional>
#include <gnutls/crypto.h>
#include <gnutls/gnutls.h>
#include <gnutls/pkcs12.h>
#include <gnutls/x509.h>
#include <iterator>
#include <memory>
#include <netdb.h>
#include <ngtcp2/ngtcp2.h>
#include <ngtcp2/ngtcp2_crypto.h>
#include <ngtcp2/ngtcp2_crypto_gnutls.h>
#include <numeric>
#include <poll.h>
#include <random>
#include <ranges>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#if CONFIG2_MINIUPNPC
#include <miniupnpc/igd_desc_parse.h>
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnpdev.h>
#include <miniupnpc/upnperrors.h>
#endif

#if FMT_VERSION >= 80000
#include <fmt/xchar.h>
#endif

constexpr int CHANNEL_COUNT = 1;
constexpr int FAILED_PASSWORD_TRIES_BEFORE_BAN = 3;

/**
 * Once ping goes above turn length * command delay,
 * the game will start 'freezing' for other clients while we catch up.
 * Since commands are sent client -> server -> client, divide by 2.
 * (duplicated in NetServer.cpp to avoid having to fetch the constants in a header file)
 */
constexpr std::uint32_t NETWORK_BAD_PING = DEFAULT_TURN_LENGTH * COMMAND_DELAY_MP / 2;

CNetServer* g_NetServer = NULL;

namespace
{
// static CStr DebugName(CNetServerSession* session)
// {
// 	if (session == NULL)
// 		return "[unknown host]";
// 	if (session->GetGUID().empty())
// 		return "[unauthed host]";
// 	return "[" + session->GetGUID().substr(0, 8) + "...]";
// }

bool TestServerSocket(const int fd, const addrinfo& info)
{
	const int error{fcntl(fd, F_SETFL, O_NONBLOCK)};
	if (error == -1)
		return false;

	// Allow IPv4 using the same socket.
	const int onlyIPv6{0};
	if (setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &onlyIPv6, sizeof(onlyIPv6)))
		return false;

	if (!bind(fd, info.ai_addr, info.ai_addrlen))
		return true;

	if (errno == EADDRINUSE)
		throw std::runtime_error{g_L10n.Translate("The port is already in use")};

	return false;
}

void GeneratePkcs12File(const std::filesystem::path& keyfile)
{
	gnutls_x509_privkey_t newKey;
	if (const int ret{gnutls_x509_privkey_init(&newKey)})
	{
		throw std::runtime_error{fmt::format("Error initializing privatkey: {}",
			gnutls_strerror(ret))};
	}
	std::unique_ptr<gnutls_x509_privkey_int, decltype(&gnutls_x509_privkey_deinit)> keyDeleter(
		newKey, gnutls_x509_privkey_deinit);

	if (const int ret{gnutls_x509_privkey_generate(newKey, GNUTLS_PK_EDDSA_ED25519, 256, 0)})
	{
		throw std::runtime_error{fmt::format("Error generating privatkey: {}",
			gnutls_strerror(ret))};
	}

	gnutls_datum_t exportedKey;
	if (const int ret{gnutls_x509_privkey_export2_pkcs8(newKey, GNUTLS_X509_FMT_DER, nullptr, 0,
		&exportedKey)})
	{
		throw std::runtime_error{fmt::format("Error exporting privatkey: {}",
			gnutls_strerror(ret))};
	}
	const std::unique_ptr<unsigned char, decltype(gnutls_free)> exportedKeyDeleter{exportedKey.data,
		gnutls_free};

	gnutls_x509_crt_t newCertificate;
	if (const int ret{gnutls_x509_crt_init(&newCertificate)})
	{
		throw std::runtime_error{fmt::format("Error initializing certificate: {}",
			gnutls_strerror(ret))};
	}
	std::unique_ptr<gnutls_x509_crt_int, decltype(&gnutls_x509_crt_deinit)> certificateDeleter(
		newCertificate, gnutls_x509_crt_deinit);

	std::array<unsigned char, 20> serialNumber;
	auto generator = [&, randomDevice = std::random_device{},
		distribution = std::uniform_int_distribution<unsigned char>{}]() mutable
	{
		return distribution(randomDevice);
	};
	std::ranges::generate(serialNumber, std::ref(generator));
	std::get<0>(serialNumber) &= ~(1 << 8);
	if (const int ret{gnutls_x509_crt_set_serial(newCertificate, serialNumber.data(),
		serialNumber.size())})
	{
		throw std::runtime_error{fmt::format("Error setting serial number: {}", gnutls_strerror(ret))};
	}

	if (const int ret{gnutls_x509_crt_set_activation_time(newCertificate, 0)})
	{
		throw std::runtime_error{fmt::format("Error setting activation time: {}",
			gnutls_strerror(ret))};
	}
	if (const int ret{gnutls_x509_crt_set_expiration_time(newCertificate, -1)})
	{
		throw std::runtime_error{fmt::format("Error setting expiration time: {}",
			gnutls_strerror(ret))};
	}

	if (const int ret{gnutls_x509_crt_set_key(newCertificate, newKey)})
	{
		throw std::runtime_error{fmt::format("Error setting certificate key: {}",
			gnutls_strerror(ret))};
	}

	if (const int ret{gnutls_x509_crt_sign(newCertificate, newCertificate, newKey)})
		throw std::runtime_error{fmt::format("Error signing certificate: {}", gnutls_strerror(ret))};

	gnutls_datum_t exportedCertificate;
	if (const int ret{gnutls_x509_crt_export2(newCertificate, GNUTLS_X509_FMT_DER,
		&exportedCertificate)})
	{
		throw std::runtime_error{fmt::format("Error exporting certificate: {}", gnutls_strerror(ret))};
	}
	const std::unique_ptr<unsigned char, decltype(gnutls_free)> exportedCertificateDeleter{
		exportedCertificate.data, gnutls_free};

	gnutls_pkcs12_t container;
	if (const int ret{gnutls_pkcs12_init(&container)})
	{
		throw std::runtime_error{fmt::format("Error initializing PKCS#12 structure: {}",
			gnutls_strerror(ret))};
	}
	std::unique_ptr<gnutls_pkcs12_int, decltype(&gnutls_pkcs12_deinit)> pkcs12Deleter{container,
		gnutls_pkcs12_deinit};

	gnutls_pkcs12_bag_t newCertificateBag;
	if (const int ret{gnutls_pkcs12_bag_init(&newCertificateBag)})
	{
		throw std::runtime_error{fmt::format("Error initializing certificate bag: {}",
			gnutls_strerror(ret))};
	}
	std::unique_ptr<gnutls_pkcs12_bag_int, decltype(&gnutls_pkcs12_bag_deinit)>
		certificateBagDeleter{newCertificateBag, gnutls_pkcs12_bag_deinit};

	if (const int ret{gnutls_pkcs12_bag_set_data(newCertificateBag, GNUTLS_BAG_CERTIFICATE,
		&exportedCertificate)})
	{
		throw std::runtime_error{fmt::format("Error adding certificate to bag: {}",
			gnutls_strerror(ret))};
	}
	if (const int ret{gnutls_pkcs12_set_bag(container, newCertificateBag)})
	{
		throw std::runtime_error{fmt::format("Error adding certificate bag: {}",
			gnutls_strerror(ret))};
	}

	gnutls_pkcs12_bag_t newKeyBag;
	if (const int ret{gnutls_pkcs12_bag_init(&newKeyBag)})
		throw std::runtime_error{fmt::format("Error initializing key bag: {}", gnutls_strerror(ret))};
	std::unique_ptr<gnutls_pkcs12_bag_int, decltype(&gnutls_pkcs12_bag_deinit)> keyBagDeleter{
		newKeyBag, gnutls_pkcs12_bag_deinit};

	if (const int ret{gnutls_pkcs12_bag_set_data(newKeyBag, GNUTLS_BAG_PKCS8_KEY, &exportedKey)})
		throw std::runtime_error{fmt::format("Error adding key to bag: {}", gnutls_strerror(ret))};
	if (const int ret{gnutls_pkcs12_set_bag(container, newKeyBag)})
		throw std::runtime_error{fmt::format("Error adding key bag: {}", gnutls_strerror(ret))};

	// Set a mac so in conforms
	if (const int ret{gnutls_pkcs12_generate_mac(container, "")})
	{
		throw std::runtime_error{fmt::format("Error generating PKCS#12 MAC: {}",
			gnutls_strerror(ret))};
	}

	gnutls_datum_t exportedPkcs12;
	if (const int ret{gnutls_pkcs12_export2(container, GNUTLS_X509_FMT_DER, &exportedPkcs12)})
	{
		throw std::runtime_error{fmt::format("Error exporting PKCS#12 structure: {}",
			gnutls_strerror(ret))};
	}
	const std::unique_ptr<unsigned char, decltype(gnutls_free)> exportedPkcs12Deleter{
		exportedPkcs12.data, gnutls_free};

	std::ofstream{keyfile, std::ios::binary}.write(reinterpret_cast<const char*>(exportedPkcs12.data),
		exportedPkcs12.size);
}

std::unique_ptr<gnutls_certificate_credentials_st, CredentialsDeleter> CreateTlsServerCredentials()
{
	gnutls_certificate_credentials_t temp;
	if (const int ret{gnutls_certificate_allocate_credentials(&temp)})
	{
		throw std::runtime_error{fmt::format("gnutls_certificate_allocate_credentials: {}",
			gnutls_strerror(ret))};
	}
	std::unique_ptr<gnutls_certificate_credentials_st, CredentialsDeleter> cred{temp};


	const OsPath keyfile = Paths(g_CmdLineArgs).UserData() / "encryption.p12";

	if (const int ret{gnutls_certificate_set_x509_simple_pkcs12_file(cred.get(),
		keyfile.string8().c_str(), GNUTLS_X509_FMT_DER, nullptr)})
	{
		throw std::runtime_error{fmt::format(
			"gnutls_certificate_set_x509_simple_pkcs12_file: {}", gnutls_strerror(ret))};
	}

	return cred;
}

std::string ComputeCertificateFingerprint(const std::filesystem::path& keyfile)
{
	const std::uintmax_t fileSize{std::filesystem::file_size(keyfile)};
	std::ifstream file{keyfile, std::ios::binary};
	std::vector<std::uint8_t> buffer(static_cast<std::size_t>(fileSize));
	file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

	gnutls_datum_t p12Data{ .data{buffer.data()}, .size{static_cast<unsigned int>(buffer.size())} };

	gnutls_pkcs12_t p12;
	if (const int ret{gnutls_pkcs12_init(&p12)})
		throw std::runtime_error{fmt::format("gnutls_pkcs12_init: {}", gnutls_strerror(ret))};
	const std::unique_ptr<gnutls_pkcs12_int, decltype(&gnutls_pkcs12_deinit)> p12Deleter{p12,
		gnutls_pkcs12_deinit};

	if (const int ret{gnutls_pkcs12_import(p12, &p12Data, GNUTLS_X509_FMT_DER, 0)})
		throw std::runtime_error{fmt::format("gnutls_pkcs12_import: {}", gnutls_strerror(ret))};

	if (const int ret{gnutls_pkcs12_verify_mac(p12, "")})
		throw std::runtime_error{fmt::format("Corrupted PKCS #12 file: {}", gnutls_strerror(ret))};

	gnutls_x509_privkey_t key{};
	gnutls_x509_crt_t* chain{};
	unsigned int chainLen{};
	if (const int ret{gnutls_pkcs12_simple_parse(p12, nullptr, &key, &chain, &chainLen, nullptr, nullptr,
		nullptr, GNUTLS_PKCS12_SP_INCLUDE_SELF_SIGNED)})
	{
		throw std::runtime_error{fmt::format("gnutls_pkcs12_simple_parse: {}", gnutls_strerror(ret))};
	}
	gnutls_x509_privkey_deinit(key);

	const std::unique_ptr<gnutls_x509_crt_t, decltype(gnutls_free)> chainDeleter{chain, gnutls_free};

	if (!chain || chainLen == 0)
		throw std::runtime_error{"No certificate found in PKCS #12 file"};

	const gnutls_x509_crt_t certificate = chain[0];
	const std::unique_ptr<gnutls_x509_crt_int, decltype(&gnutls_x509_crt_deinit)> certificateDeleter{
		certificate, &gnutls_x509_crt_deinit};

	// Export the certificate to DER so that it can be fingerprinted.
	gnutls_datum_t der{};
	if (const int ret{gnutls_x509_crt_export2(certificate, GNUTLS_X509_FMT_DER, &der)})
		throw std::runtime_error{fmt::format("gnutls_x509_crt_export2: {}", gnutls_strerror(ret))};

	const std::unique_ptr<unsigned char, decltype(gnutls_free)> derDeleter{der.data, gnutls_free};

	std::array<std::uint8_t, 64> fingerprint;
	std::size_t fingerprintSize{sizeof(fingerprint)};
	if (const int ret{gnutls_fingerprint(GNUTLS_DIG_SHA256, &der, fingerprint.data(), &fingerprintSize)})
		throw std::runtime_error{fmt::format("gnutls_fingerprint: {}", gnutls_strerror(ret))};

	return Hexify(fingerprint.data(), fingerprintSize);
}
} // anonymous namespace

class CNetServerWorker::Impl
{
public:
	explicit Impl(const CreateSocketResult socket):
		m_LocalAddress{socket}
	{}
	~Impl()
	{
		close(m_LocalAddress.descriptor);
	}
	CreateSocketResult m_LocalAddress;
	std::unique_ptr<gnutls_certificate_credentials_st, CredentialsDeleter> m_Credentials{
		CreateTlsServerCredentials()};
	ngtcp2_settings m_Settings{InitSettings()};

	void HandleIncoming(CNetServerWorker& server)
	{
		while (true)
		{
			AddressStorage remoteAddress;
			std::array<std::uint8_t, MAX_UDP_PAYLOAD_SIZE> buf;

			std::size_t n_read;
			try
			{
				n_read = ReceiveDatagram(m_LocalAddress.descriptor, buf, remoteAddress);
			}
			catch (std::system_error& e)
			{
				if (e.code().value() == EAGAIN || e.code().value() == EWOULDBLOCK)
					return;
				throw;
			}

			ngtcp2_version_cid version;
			if (const int ret{ngtcp2_pkt_decode_version_cid(&version, buf.data(), n_read,
				CID_LENGTH)})
			{
				LOGWARNING("Received wrongly formatted QUIC header: {}", ngtcp2_strerror(ret));
				continue;
			}

			const ngtcp2_addr remote{
				.addr{&remoteAddress.address.sa},
				.addrlen{remoteAddress.length}
			};

			/* Find any existing connection by DCID */
			ngtcp2_cid tempCid;
			ngtcp2_cid_init(&tempCid, version.dcid, version.dcidlen);

			const auto connectionIter = std::ranges::find_if(server.m_Sessions,
				[&tempCid](const std::vector<ngtcp2_cid> association)
				{
					return std::ranges::any_of(association, [&tempCid](const ngtcp2_cid& elem)
					{
						return ngtcp2_cid_eq(&elem, &tempCid);
					});
				},
				&CNetServerSession::GetAllCids);

			const bool existing{connectionIter != server.m_Sessions.end()};
			if (!existing)
			{
				if (PS::contains(server.m_BannedIPs,
					std::to_array(remoteAddress.address.in6.sin6_addr.s6_addr)))
				{
					continue;
				}

				ngtcp2_pkt_hd header;
				if (const int ret{ngtcp2_accept(&header, buf.data(), n_read)})
				{
					LOGERROR("Package not accepted by new connection: {}", ngtcp2_strerror(ret));
					continue;
				}

				const ngtcp2_cid newScid{GenerateRandomCid()};

				const ngtcp2_path path{
					.local{
						.addr{&m_LocalAddress.address.address.sa},
						.addrlen{m_LocalAddress.address.length}
					},
					.remote{remote}
				};

				server.m_Sessions.push_back(std::make_unique<CNetServerSession>(server,
					m_LocalAddress.descriptor, m_Settings, m_Credentials.get(), header, newScid, path));
				server.SetupSession(server.m_Sessions.back().get());
			}
			auto& session{existing ? *connectionIter : server.m_Sessions.back()};

			try
			{
				session->Read(remote, {buf.data(), n_read});
			}
			catch (const std::system_error&)
			{
				throw;
			}
			catch (const std::runtime_error&)
			{
				const auto session = existing ? std::move(*connectionIter) :
					std::move(server.m_Sessions.back());
				if (existing)
					server.m_Sessions.erase(connectionIter);
				else
					server.m_Sessions.pop_back();

				session->Update(static_cast<uint>(NMT_CONNECTION_LOST), nullptr);
			}
		}
	}
};

/*
 * XXX: We use some non-threadsafe functions from the worker thread.
 * See https://gitea.wildfiregames.com/0ad/0ad/issues/654
 */

CNetServerWorker::CNetServerWorker(const bool continueSavedGame, std::uint16_t port,
	const bool useLobbyAuth, std::string password, std::string controllerSecret,
	std::string initAttributes) :
	m_ContinuesSavedGame{continueSavedGame},
	m_LobbyAuth{useLobbyAuth},
	m_ControllerSecret{std::move(controllerSecret)},
	m_Password{std::move(password)}
{
	m_Stats = std::make_unique<CNetStatsTable>();
	if (CProfileViewer::IsInitialised())
		g_ProfileViewer.AddRootTable(m_Stats.get());

	m_State = SERVER_STATE_PREGAME;

	const CreateSocketResult socket{CreateSocket(nullptr, port,
		addrinfo{ .ai_flags{AI_PASSIVE | AI_V4MAPPED }, .ai_family{AF_INET6} }, TestServerSocket)};
	m_SocketDescriptor = socket.descriptor;
	m_LocalPort = ntohs(socket.address.address.in6.sin6_port);

	const auto keyfile = (Paths(g_CmdLineArgs).UserData() / "encryption.p12").fileSystemPath();
	if (!std::filesystem::is_regular_file(keyfile))
		GeneratePkcs12File(keyfile);
	m_CertificateFingerprint = ComputeCertificateFingerprint(keyfile);

	// Launch the worker thread
	m_WorkerThread = std::thread(Threading::HandleExceptions<RunThread>::Wrapper, this,
		std::move(initAttributes), socket);

#if CONFIG2_MINIUPNPC
	// Launch the UPnP thread
	m_UPnPThread = std::thread(Threading::HandleExceptions<SetupUPnP>::Wrapper, port);
#endif
}

CNetServerWorker::~CNetServerWorker()
{
	// Tell the thread to shut down
	{
		std::lock_guard<std::mutex> lock(m_WorkerMutex);
		m_Shutdown = true;
	}

	// Wait for it to shut down cleanly
	m_WorkerThread.join();

#if CONFIG2_MINIUPNPC
	if (m_UPnPThread.joinable())
		m_UPnPThread.detach();
#endif
}


bool CNetServerWorker::CheckPassword(const std::string& password, const std::string& salt) const
{
	return HashCryptographically(m_Password, salt) == password;
}

#if CONFIG2_MINIUPNPC
void CNetServerWorker::SetupUPnP(const std::uint16_t port)
{
	debug_SetThreadName("UPnP");

	// Values we want to set.
	char psPort[6];
	sprintf_s(psPort, ARRAY_SIZE(psPort), "%d", port);
	const char* leaseDuration = "0"; // Indefinite/permanent lease duration.
	const char* description = "0AD Multiplayer";
	const char* protocall = "UDP";
	char internalIPAddress[64];
	char externalIPAddress[40];

	// Variables to hold the values that actually get set.
	char intClient[40];
	char intPort[6];
	char duration[16];

	// Intermediate variables.
	bool allocatedUrls = false;
	struct UPNPUrls urls;
	struct IGDdatas data;
	struct UPNPDev* devlist = NULL;

	// Make sure everything is properly freed.
	std::function<void()> freeUPnP = [&allocatedUrls, &urls, &devlist]()
	{
		if (allocatedUrls)
			FreeUPNPUrls(&urls);
		freeUPNPDevlist(devlist);
		// IGDdatas does not need to be freed according to UPNP_GetIGDFromUrl
	};

	// Cached root descriptor URL.
	const std::string rootDescURL{g_ConfigDB.Get("network.upnprootdescurl", std::string{})};
	if (!rootDescURL.empty())
		LOGMESSAGE("Net server: attempting to use cached root descriptor URL: {}", rootDescURL);

	int ret = 0;

	// Try a cached URL first
	if (!rootDescURL.empty() && UPNP_GetIGDFromUrl(rootDescURL.c_str(), &urls, &data, internalIPAddress, sizeof(internalIPAddress)) && strlen(data.first.controlurl) != 0)
	{
		LOGMESSAGE("Net server: using cached IGD = {}", urls.controlURL);
		ret = 1;
	}
	// No cached URL, or it did not respond. Try discovering the UPnP IGD for 2 seconds.
#if defined(MINIUPNPC_API_VERSION) && MINIUPNPC_API_VERSION >= 14
	else if ((devlist = upnpDiscover(2000, 0, 0, 0, 0, 2, 0)) != NULL)
#else
	else if ((devlist = upnpDiscover(2000, 0, 0, 0, 0, 0)) != NULL)
#endif
	{
#if defined(MINIUPNPC_API_VERSION) && MINIUPNPC_API_VERSION >= 18
		ret = UPNP_GetValidIGD(devlist, &urls, &data, internalIPAddress, sizeof(internalIPAddress), nullptr, 0);
#else
		ret = UPNP_GetValidIGD(devlist, &urls, &data, internalIPAddress, sizeof(internalIPAddress));
#endif
		allocatedUrls = ret != 0; // urls is allocated on non-zero return values
	}
	else
	{
		LOGMESSAGE("Net server: upnpDiscover failed and no working cached URL.");
		freeUPnP();
		return;
	}

	switch (ret)
	{
	case 0:
		LOGMESSAGE("Net server: No IGD found");
		break;
	case 1:
		LOGMESSAGE("Net server: found valid IGD = {}", urls.controlURL);
		break;
#if defined(MINIUPNPC_API_VERSION) && MINIUPNPC_API_VERSION >= 18
	case 2:
		LOGMESSAGE("Net server: found IGD with reserved IP = {}, will try to continue anyway",
			urls.controlURL);
		break;
	case 3:
#else
	case 2:
#endif
		LOGMESSAGE("Net server: found a valid, not connected IGD = {}, will try to continue anyway",
			urls.controlURL);
		break;
#if defined(MINIUPNPC_API_VERSION) && MINIUPNPC_API_VERSION >= 18
	case 4:
#else
	case 3:
#endif
		LOGMESSAGE("Net server: found a UPnP device unrecognized as IGD = {}, will try to continue "
			"anyway", urls.controlURL);
		break;
	default:
		debug_warn(L"Unrecognized return value from UPNP_GetValidIGD");
	}

	// Try getting our external/internet facing IP. TODO: Display this on the game-setup page for convenience.
	ret = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
	if (ret != UPNPCOMMAND_SUCCESS)
	{
		LOGMESSAGE("Net server: GetExternalIPAddress failed with code {} ({})", ret,
			strupnperror(ret));
		freeUPnP();
		return;
	}
	LOGMESSAGE("Net server: ExternalIPAddress = {}", externalIPAddress);

	// Try to setup port forwarding.
	ret = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, psPort, psPort,
							internalIPAddress, description, protocall, 0, leaseDuration);
	if (ret != UPNPCOMMAND_SUCCESS)
	{
		LOGMESSAGE("Net server: AddPortMapping({}, {}, {}) failed with code {} ({})",
			psPort, psPort, internalIPAddress, ret, strupnperror(ret));
		freeUPnP();
		return;
	}

	// Check that the port was actually forwarded.
	ret = UPNP_GetSpecificPortMappingEntry(urls.controlURL,
									 data.first.servicetype,
									 psPort, protocall,
#if defined(MINIUPNPC_API_VERSION) && MINIUPNPC_API_VERSION >= 10
									 NULL/*remoteHost*/,
#endif
									 intClient, intPort, NULL/*desc*/,
									 NULL/*enabled*/, duration);

	if (ret != UPNPCOMMAND_SUCCESS)
	{
		LOGMESSAGE("Net server: GetSpecificPortMappingEntry() failed with code {} ({})", ret,
			strupnperror(ret));
		freeUPnP();
		return;
	}

	LOGMESSAGE("Net server: External {}:{} {} is redirected to internal {}:{} (duration={})",
		externalIPAddress, psPort, protocall, intClient, intPort, duration);

	// Cache root descriptor URL to try to avoid discovery next time.
	g_ConfigDB.SetValueString(CFG_USER, "network.upnprootdescurl", urls.rootdescURL);
	g_ConfigDB.WriteValueToFile(CFG_USER, "network.upnprootdescurl", urls.rootdescURL);
	LOGMESSAGE("Net server: cached UPnP root descriptor URL as {}", urls.rootdescURL);

	freeUPnP();
}
#endif // CONFIG2_MINIUPNPC

bool CNetServerWorker::Multicast(const CNetMessage* message,
	const std::vector<NetServerSessionState>& targetStates,
	const std::optional<std::vector<std::string>>& receivers /* = std::nullopt */)
{
	const auto isReceiver = [&](const CNetServerSession& session)
		{
			if (!PS::contains(targetStates,
				static_cast<NetServerSessionState>(session.GetCurrState())))
			{
				return false;
			}
			if (!receivers)
				return true;
			return PS::contains(*receivers, session.GetGUID());
		};

	bool ok = true;

	// TODO: this does lots of repeated message serialisation if we have lots
	// of remote peers; could do it more efficiently if that's a real problem

	for (const auto& session : m_Sessions)
		if (isReceiver(*session) && !session->SendMessage(message))
			ok = false;

	return ok;
}

void CNetServerWorker::RunThread(CNetServerWorker* data, const std::string& initAttributes,
	const CreateSocketResult& socket)
{
	debug_SetThreadName("NetServer");

	data->Run(initAttributes, socket);
}

void CNetServerWorker::Run(const std::string& initAttributes, const CreateSocketResult& socket)
{
	// The script context uses the profiler and therefore the thread must be registered before the context is created
	g_Profiler2.RegisterCurrentThread("Net server");

	// We create a new Script::Context for this network thread, with a single Script::Interface.
	Script::Context netServerContext;
	m_ScriptInterface = new Script::Interface("Engine", "Net server", netServerContext);
	m_InitAttributes.init(m_ScriptInterface->GetGeneralJSContext(), JS::UndefinedValue());

	if (!initAttributes.empty())
	{
		Script::Request rq(m_ScriptInterface);
		JS::RootedValue gameAttributesVal(rq.cx);
		Script::ParseJSON(rq, std::move(initAttributes), &gameAttributesVal);
		m_InitAttributes = gameAttributesVal;
	}

	Impl impl{socket};

	while (true)
	{
		if (!RunStep(impl))
			break;

		// Update profiler stats
		m_Stats->LatchHostState(m_Sessions);
	}

	// Clear roots before deleting their context
	m_SavedCommands.clear();

	SAFE_DELETE(m_ScriptInterface);

	for (const auto& session : m_Sessions)
		session->Disconnect(NDR_SERVER_SHUTDOWN);
}

bool CNetServerWorker::RunStep(Impl& impl)
{
	// Check for messages from the game thread.
	// (Do as little work as possible while the mutex is held open,
	// to avoid performance problems and deadlocks.)

	m_ScriptInterface->GetContext().MaybeIncrementalGC();

	Script::Request rq(m_ScriptInterface);

	std::vector<bool> newStartGame;
	std::vector<std::pair<CStr, CStr>> newLobbyAuths;
	std::vector<std::uint32_t> newTurnLength;

	{
		std::lock_guard<std::mutex> lock(m_WorkerMutex);

		if (m_Shutdown)
			return false;

		newStartGame.swap(m_StartGameQueue);
		newLobbyAuths.swap(m_LobbyAuthQueue);
		newTurnLength.swap(m_TurnLengthQueue);
	}

	if (!newTurnLength.empty())
		SetTurnLength(newTurnLength.back());

	while (!newLobbyAuths.empty())
	{
		const std::pair<CStr, CStr>& auth = newLobbyAuths.back();
		ProcessLobbyAuth(auth.first, auth.second);
		newLobbyAuths.pop_back();
	}

	// Perform file transfers
	for (const auto& session : m_Sessions)
		session->GetFileTransferer().Poll();

	CheckClientConnections();

	pollfd pollFd{
		.fd{impl.m_LocalAddress.descriptor},
		.events{POLLIN | POLLOUT}
	};
	const int ready{poll(&pollFd, 1, 25)};

	if (ready < 0)
		throw std::runtime_error{fmt::format("poll: {}", std::strerror(errno))};

	if (ready > 0 && pollFd.revents & POLLIN)
		impl.HandleIncoming(*this);

	for (auto& session : m_Sessions)
	{
		try
		{
			session->TrySend(ready == 0, pollFd.revents & POLLOUT, impl.m_LocalAddress.descriptor);
		}
		catch (const std::exception& e)
		{
			LOGERROR("{}", e.what());
			std::exchange(session, nullptr)->Disconnect(NDR_UNKNOWN);
		}
	}

	std::erase(m_Sessions, nullptr);

	return true;
}

void CNetServerWorker::CheckClientConnections()
{
	// Send messages at most once per second
	std::time_t now = std::time(nullptr);
	if (now <= m_LastConnectionCheck)
		return;

	m_LastConnectionCheck = now;

	for (size_t i = 0; i < m_Sessions.size(); ++i)
	{
		std::uint32_t lastReceived = m_Sessions[i]->GetLastReceivedTime();
		std::uint32_t meanRTT = m_Sessions[i]->GetMeanRTT();

		CNetMessage* message = nullptr;

		// Report if we didn't hear from the client since few seconds
		if (lastReceived > NETWORK_WARNING_TIMEOUT)
		{
			CClientTimeoutMessage* msg = new CClientTimeoutMessage();
			msg->m_GUID = m_Sessions[i]->GetGUID();
			msg->m_LastReceivedTime = lastReceived;
			message = msg;
		}
		// Report if the client has bad ping
		else if (meanRTT > NETWORK_BAD_PING)
		{
			CClientPerformanceMessage* msg = new CClientPerformanceMessage();
			CClientPerformanceMessage::S_m_Clients client;
			client.m_GUID = m_Sessions[i]->GetGUID();
			client.m_MeanRTT = meanRTT;
			msg->m_Clients.push_back(client);
			message = msg;
		}

		// Send to all clients except the affected one
		// (since that will show the locally triggered warning instead).
		// Also send it to clients that finished the loading screen while
		// the game is still waiting for other clients to finish the loading screen.
		if (message)
			for (size_t j = 0; j < m_Sessions.size(); ++j)
			{
				if (i != j && (
				    (m_Sessions[j]->GetCurrState() == NSS_PREGAME && m_State == SERVER_STATE_PREGAME) ||
				    m_Sessions[j]->GetCurrState() == NSS_INGAME))
				{
					m_Sessions[j]->SendMessage(message);
				}
			}

		SAFE_DELETE(message);
	}
}

void CNetServerWorker::HandleMessageReceive(CNetMessage* message, CNetServerSession* session)
{
	// Handle non-FSM messages first
	Status status = session->GetFileTransferer().HandleMessageReceive(*message);
	if (status != INFO::SKIPPED)
		return;

	if (message->GetType() == NMT_FILE_TRANSFER_REQUEST)
	{
		CFileTransferRequestMessage* reqMessage = (CFileTransferRequestMessage*)message;

		// A client requested the gamestate. Clients only request the gamestate when we sent them a
		// JoinSyncStart or a GameSavedStart message. We only send those messages after we received the
		// gamestate.
		// For joins and loads the gamestate is in a different format. Send the respective one.

		session->GetFileTransferer().StartResponse(reqMessage->m_RequestID,
			static_cast<CNetFileTransferer::RequestType>(reqMessage->m_RequestType) ==
				CNetFileTransferer::RequestType::LOADGAME ? m_SavedState : m_JoinSyncFile);

		return;
	}

	// Update FSM
	if (!session->Update(message->GetType(), message))
		LOGERROR("Net server: Error running FSM update (type={} state={})",
			static_cast<int>(message->GetType()), static_cast<int>(session->GetCurrState()));
}

void CNetServerWorker::SetupSession(CNetServerSession* session)
{
	// Set up transitions for session

	session->AddTransition(NSS_UNCONNECTED, static_cast<unsigned int>(NMT_CONNECTION_LOST),
		NSS_UNCONNECTED);

	session->AddTransition(NSS_HANDSHAKE, static_cast<unsigned int>(NMT_CONNECTION_LOST),
		NSS_UNCONNECTED);
	session->AddTransition(NSS_HANDSHAKE, static_cast<unsigned int>(NMT_CLIENT_HANDSHAKE),
		NSS_AUTHENTICATE, &OnClientHandshake, session);

	session->AddTransition(NSS_LOBBY_AUTHENTICATE, static_cast<unsigned int>(NMT_CONNECTION_LOST),
		NSS_UNCONNECTED);
	session->AddTransition(NSS_LOBBY_AUTHENTICATE, static_cast<unsigned int>(NMT_AUTHENTICATE),
		NSS_PREGAME, &OnAuthenticate, session);

	session->AddTransition(NSS_AUTHENTICATE, static_cast<unsigned int>(NMT_CONNECTION_LOST),
		NSS_UNCONNECTED);
	session->AddTransition(NSS_AUTHENTICATE, static_cast<unsigned int>(NMT_AUTHENTICATE), NSS_PREGAME,
		&OnAuthenticate, session);

	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_CONNECTION_LOST), NSS_UNCONNECTED,
		&OnDisconnect, session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_CHAT), NSS_PREGAME, &OnChat,
		session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_READY), NSS_PREGAME, &OnReady,
		session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_CLEAR_ALL_READY), NSS_PREGAME,
		&OnClearAllReady, session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_GAME_SETUP), NSS_PREGAME,
		&OnGameSetup, session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_ASSIGN_PLAYER), NSS_PREGAME,
		&OnAssignPlayer, session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_KICKED), NSS_PREGAME,
		&OnKickPlayer, session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_GAME_START), NSS_PREGAME,
		&OnGameStart, session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_SAVED_GAME_START), NSS_PREGAME,
		&OnSavedGameStart, session);
	session->AddTransition(NSS_PREGAME, static_cast<unsigned int>(NMT_LOADED_GAME), NSS_INGAME,
		&OnLoadedGame, session);

	session->AddTransition(NSS_JOIN_SYNCING, static_cast<unsigned int>(NMT_KICKED), NSS_JOIN_SYNCING,
		&OnKickPlayer, session);
	session->AddTransition(NSS_JOIN_SYNCING, static_cast<unsigned int>(NMT_CONNECTION_LOST),
		NSS_UNCONNECTED, &OnDisconnect, session);
	session->AddTransition(NSS_JOIN_SYNCING, static_cast<unsigned int>(NMT_LOADED_GAME), NSS_INGAME,
		&OnJoinSyncingLoadedGame, session);

	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_REJOINED), NSS_INGAME, &OnRejoined,
		session);
	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_KICKED), NSS_INGAME, &OnKickPlayer,
		session);
	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_CLIENT_PAUSED), NSS_INGAME,
		&OnClientPaused, session);
	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_CONNECTION_LOST), NSS_UNCONNECTED,
		&OnDisconnect, session);
	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_CHAT), NSS_INGAME, &OnChat,
		session);
	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_SIMULATION_COMMAND), NSS_INGAME,
		&OnSimulationCommand, session);
	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_FLARE), NSS_INGAME, &OnFlare,
		session);
	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_SYNC_CHECK), NSS_INGAME,
		&OnSyncCheck, session);
	session->AddTransition(NSS_INGAME, static_cast<unsigned int>(NMT_END_COMMAND_BATCH), NSS_INGAME,
		&OnEndCommandBatch, session);

	// Set first state
	session->SetFirstState(NSS_HANDSHAKE);
}

void CNetServerWorker::OnUserJoin(CNetServerSession* session)
{
	AddPlayer(session->GetGUID(), session->GetUserName());

	CPlayerAssignmentMessage assignMessage;
	ConstructPlayerAssignmentMessage(assignMessage);
	session->SendMessage(&assignMessage);
}

void CNetServerWorker::OnUserLeave(CNetServerSession* session)
{
	std::vector<CStr>::iterator pausing = std::find(m_PausingPlayers.begin(), m_PausingPlayers.end(), session->GetGUID());
	if (pausing != m_PausingPlayers.end())
		m_PausingPlayers.erase(pausing);

	RemovePlayer(session->GetGUID());

	if (m_ServerTurnManager && session->GetCurrState() != NSS_JOIN_SYNCING)
		m_ServerTurnManager->UninitialiseClient(session->GetHostID());

	// TODO: ought to switch the player controlled by that client
	// back to AI control, or something?
}

void CNetServerWorker::AddPlayer(const CStr& guid, const CStrW& name)
{
	// Find all player IDs in active use; we mustn't give them to a second player (excluding the unassigned ID: -1)
	std::set<std::int32_t> usedIDs;
	for (const std::pair<const CStr, PlayerAssignment>& p : m_PlayerAssignments)
		if (p.second.m_Enabled && p.second.m_PlayerID != -1)
			usedIDs.insert(p.second.m_PlayerID);

	// If the player is rejoining after disconnecting, try to give them
	// back their old player ID. Don't do this in pregame however,
	// as that ID might be invalid for various reasons.

	std::int32_t playerID = -1;

	if (m_State != SERVER_STATE_PREGAME)
	{
		// Try to match GUID first
		for (PlayerAssignmentMap::iterator it = m_PlayerAssignments.begin(); it != m_PlayerAssignments.end(); ++it)
		{
			if (!it->second.m_Enabled && it->first == guid && usedIDs.find(it->second.m_PlayerID) == usedIDs.end())
			{
				playerID = it->second.m_PlayerID;
				m_PlayerAssignments.erase(it); // delete the old mapping, since we've got a new one now
				goto found;
			}
		}

		// Try to match username next
		for (PlayerAssignmentMap::iterator it = m_PlayerAssignments.begin(); it != m_PlayerAssignments.end(); ++it)
		{
			if (!it->second.m_Enabled && it->second.m_Name == name && usedIDs.find(it->second.m_PlayerID) == usedIDs.end())
			{
				playerID = it->second.m_PlayerID;
				m_PlayerAssignments.erase(it); // delete the old mapping, since we've got a new one now
				goto found;
			}
		}
	}

found:
	PlayerAssignment assignment;
	assignment.m_Enabled = true;
	assignment.m_Name = name;
	assignment.m_PlayerID = playerID;
	assignment.m_Status = 0;
	m_PlayerAssignments[guid] = assignment;

	// Send the new assignments to all currently active players
	// (which does not include the one that's just joining)
	SendPlayerAssignments();
}

void CNetServerWorker::RemovePlayer(const CStr& guid)
{
	m_PlayerAssignments[guid].m_Enabled = false;

	SendPlayerAssignments();
}

void CNetServerWorker::ClearAllPlayerReady()
{
	for (std::pair<const CStr, PlayerAssignment>& p : m_PlayerAssignments)
		if (p.second.m_Status != 2)
			p.second.m_Status = 0;

	SendPlayerAssignments();
}

void CNetServerWorker::KickPlayer(const CStrW& playerName, const bool ban)
{
	// Find the user with that name
	const auto it = std::find_if(m_Sessions.begin(), m_Sessions.end(),
		[&](const auto& session) { return session->GetUserName() == playerName; });

	// and return if no one or the host has that name
	if (it == m_Sessions.end() || (*it)->GetGUID() == m_ControllerGUID)
		return;

	if (ban)
	{
		// Remember name
		if (std::find(m_BannedPlayers.begin(), m_BannedPlayers.end(), playerName) == m_BannedPlayers.end())
			m_BannedPlayers.push_back(m_LobbyAuth ? CStrW(playerName.substr(0, playerName.find(L" ("))) : playerName);

		// Remember IP address
		const std::array<std::uint8_t, 16> ipAddress{(*it)->GetIPAddress()};
		if (std::find(m_BannedIPs.begin(), m_BannedIPs.end(), ipAddress) == m_BannedIPs.end())
			m_BannedIPs.push_back(ipAddress);
	}

	// Disconnect that user
	(*it)->Disconnect(ban ? NDR_BANNED : NDR_KICKED);

	// Send message notifying other clients
	CKickedMessage kickedMessage;
	kickedMessage.m_Name = playerName;
	kickedMessage.m_Ban = ban;
	Multicast(&kickedMessage, { NSS_PREGAME, NSS_JOIN_SYNCING, NSS_INGAME });
}

void CNetServerWorker::AssignPlayer(int playerID, const CStr& guid)
{
	// Remove anyone who's already assigned to this player
	for (std::pair<const CStr, PlayerAssignment>& p : m_PlayerAssignments)
	{
		if (p.second.m_PlayerID == playerID)
			p.second.m_PlayerID = -1;
	}

	// Update this host's assignment if it exists
	if (m_PlayerAssignments.find(guid) != m_PlayerAssignments.end())
		m_PlayerAssignments[guid].m_PlayerID = playerID;

	SendPlayerAssignments();
}

void CNetServerWorker::ConstructPlayerAssignmentMessage(CPlayerAssignmentMessage& message)
{
	for (const std::pair<const CStr, PlayerAssignment>& p : m_PlayerAssignments)
	{
		if (!p.second.m_Enabled)
			continue;

		CPlayerAssignmentMessage::S_m_Hosts h;
		h.m_GUID = p.first;
		h.m_Name = p.second.m_Name;
		h.m_PlayerID = p.second.m_PlayerID;
		h.m_Status = p.second.m_Status;
		message.m_Hosts.push_back(h);
	}
}

void CNetServerWorker::SendPlayerAssignments()
{
	CPlayerAssignmentMessage message;
	ConstructPlayerAssignmentMessage(message);
	Multicast(&message, { NSS_PREGAME, NSS_JOIN_SYNCING, NSS_INGAME });
}

const Script::Interface& CNetServerWorker::GetScriptInterface()
{
	return *m_ScriptInterface;
}

void CNetServerWorker::SetTurnLength(std::uint32_t msecs)
{
	if (m_ServerTurnManager)
		m_ServerTurnManager->SetTurnLength(msecs);
}

void CNetServerWorker::ProcessLobbyAuth(const CStr& name, const CStr& token)
{
	LOGMESSAGE("Net Server: Received lobby auth message from {} with {}", name, token);
	// Find the user with that guid
	const auto it = std::find_if(m_Sessions.begin(), m_Sessions.end(),
		[&](const auto& session) { return session->GetGUID() == token; });

	if (it == m_Sessions.end())
		return;

	(*it)->SetUserName(name.FromUTF8());
	// Send an empty message to request the authentication message from the client
	// after its identity has been confirmed via the lobby
	CAuthenticateMessage emptyMessage;
	(*it)->SendMessage(&emptyMessage);
}

bool CNetServerWorker::OnClientHandshake(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_CLIENT_HANDSHAKE));

	CNetServerWorker& server = session->GetServer();

	CCliHandshakeMessage* message = (CCliHandshakeMessage*)event->GetParamRef();
	if (message->m_ProtocolVersion != PS_PROTOCOL_VERSION)
	{
		session->Disconnect(NDR_INCORRECT_PROTOCOL_VERSION);
		return false;
	}

	if (CheckHandshake(CreateHandshake<CSrvHandshakeMessage>(), *message))
	{
		session->Disconnect(NDR_INCORRECT_SOFTWARE_VERSION);
		return false;
	}

	CStr guid = ps_generate_guid();
	int count = 0;
	// Ensure unique GUID
	while(std::find_if(
		server.m_Sessions.begin(), server.m_Sessions.end(),
		[&guid] (const auto& session)
		{ return session->GetGUID() == guid; }) != server.m_Sessions.end())
	{
		if (++count > 100)
		{
			session->Disconnect(NDR_GUID_FAILED);
			return true;
		}
		guid = ps_generate_guid();
	}

	session->SetGUID(guid);

	CSrvHandshakeResponseMessage handshakeResponse;
	handshakeResponse.m_UseProtocolVersion = PS_PROTOCOL_VERSION;
	handshakeResponse.m_GUID = guid;
	handshakeResponse.m_Flags = 0;

	if (server.m_LobbyAuth)
	{
		handshakeResponse.m_Flags |= PS_NETWORK_FLAG_REQUIRE_LOBBYAUTH;
		session->SetNextState(NSS_LOBBY_AUTHENTICATE);
	}

	session->SendMessage(&handshakeResponse);

	return true;
}

bool CNetServerWorker::OnAuthenticate(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_AUTHENTICATE));

	CNetServerWorker& server = session->GetServer();

	// Prohibit joins while the game is loading
	if (server.m_State == SERVER_STATE_LOADING)
	{
		LOGMESSAGE("Refused connection while the game is loading");
		session->Disconnect(NDR_SERVER_LOADING);
		return true;
	}

	CAuthenticateMessage* message = (CAuthenticateMessage*)event->GetParamRef();
	CStrW username = SanitisePlayerName(message->m_Name);
	CStrW usernameWithoutRating(username.substr(0, username.find(L" (")));

	// Compare the lowercase names as specified by https://xmpp.org/extensions/xep-0029.html#sect-idm139493404168176
	// "[...] comparisons will be made in case-normalized canonical form."
	if (server.m_LobbyAuth && usernameWithoutRating.LowerCase() != session->GetUserName().LowerCase())
	{
		LOGERROR("Net server: lobby auth: {} tried joining as {}", session->GetUserName().ToUTF8(),
			usernameWithoutRating.ToUTF8());
		session->Disconnect(NDR_LOBBY_AUTH_FAILED);
		return true;
	}

	// Check the password before anything else.
	// NB: m_Name must match the client's salt, @see CNetClient::SetGamePassword
	if (!server.CheckPassword(message->m_Password, message->m_Name.ToUTF8()))
	{
		// Noisy logerror because players are not supposed to be able to get the IP,
		// so this might be someone targeting the host for some reason
		// (or TODO a dedicated server and we do want to log anyways)
		LOGERROR("Net server: user {} tried joining with the wrong password",
			session->GetUserName().ToUTF8());
		session->Disconnect(NDR_SERVER_REFUSED);
		return true;
	}

	// If lobby authentication is enabled, the clients playername has already been registered.
	// There also can't be any duplicated names.
	// Either deduplicate or prohibit join if name is in use
	if (!server.m_LobbyAuth && g_ConfigDB.Get("network.duplicateplayernames", false))
		username = server.DeduplicatePlayerName(username);
	else
	{
		const auto it = std::find_if(
			server.m_Sessions.begin(), server.m_Sessions.end(),
			[&username](const auto& session) { return session->GetUserName() == username; });

		if (it != server.m_Sessions.end() && it->get() != session)
		{
			session->Disconnect(NDR_PLAYERNAME_IN_USE);
			return true;
		}
	}

	// Disconnect banned usernames
	if (std::find(server.m_BannedPlayers.begin(), server.m_BannedPlayers.end(), server.m_LobbyAuth ? usernameWithoutRating : username) != server.m_BannedPlayers.end())
	{
		session->Disconnect(NDR_BANNED);
		return true;
	}

	bool isRejoining = false;
	// Don't check for maxObservers in the gamesetup, as we don't know yet who will be assigned
	if (server.m_State != SERVER_STATE_PREGAME)
	{
		bool isObserver = true;
		int disconnectedPlayers = 0;
		int connectedPlayers = 0;
		// (TODO: if GUIDs were stable, we should use them instead)
		for (const std::pair<const CStr, PlayerAssignment>& p : server.m_PlayerAssignments)
		{
			const PlayerAssignment& assignment = p.second;

			if (!assignment.m_Enabled && assignment.m_Name == username)
			{
				isObserver = assignment.m_PlayerID == -1;
				isRejoining = true;
			}

			if (assignment.m_PlayerID == -1)
				continue;

			if (assignment.m_Enabled)
				++connectedPlayers;
			else
				++disconnectedPlayers;
		}

		// Optionally allow everyone or only buddies to join after the game has started
		if (!isRejoining)
		{
			const std::string observerLateJoin{
				g_ConfigDB.Get("network.lateobservers", std::string{})};

			if (observerLateJoin == "everyone")
			{
				isRejoining = true;
			}
			else if (observerLateJoin == "buddies")
			{
				std::wstringstream buddiesStream(wstring_from_utf8(
					g_ConfigDB.Get("lobby.buddies", std::string{})));
				CStrW buddy;
				while (std::getline(buddiesStream, buddy, L','))
				{
					if (buddy == usernameWithoutRating)
					{
						isRejoining = true;
						break;
					}
				}
			}
		}

		if (!isRejoining)
		{
			LOGMESSAGE("Refused connection after game start from not-previously-known user \"{}\"",
				utf8_from_wstring(username));
			session->Disconnect(NDR_SERVER_ALREADY_IN_GAME);
			return true;
		}

		// Ensure all players will be able to rejoin
		if (isObserver && static_cast<int>(server.m_Sessions.size()) - connectedPlayers >
			g_ConfigDB.Get("network.observerlimit", 0))
		{
			session->Disconnect(NDR_SERVER_FULL);
			return true;
		}
	}

	std::uint32_t newHostID = server.m_NextHostID++;

	session->SetUserName(username);
	session->SetHostID(newHostID);

	CAuthenticateResultMessage authenticateResult;
	authenticateResult.m_Code = isRejoining ? ARC_OK_REJOINING :
		server.m_ContinuesSavedGame ? ARC_OK_SAVED_GAME : ARC_OK;
	authenticateResult.m_HostID = newHostID;
	authenticateResult.m_Message = L"Logged in";
	authenticateResult.m_IsController = 0;

	if (message->m_ControllerSecret == server.m_ControllerSecret)
	{
		if (server.m_ControllerGUID.empty())
		{
			server.m_ControllerGUID = session->GetGUID();
			authenticateResult.m_IsController = 1;
		}
		// TODO: we could probably handle having several controllers, or swapping?
	}

	session->SendMessage(&authenticateResult);

	server.OnUserJoin(session);

	if (!isRejoining)
		return true;

	ENSURE(server.m_State != SERVER_STATE_PREGAME);

	// Request a copy of the current game state from an existing player, so we can send it on to the new
	// player.

	// Assume session 0 is most likely the local player, so they're the most efficient client to request a
	// copy from.
	server.m_Sessions.at(0)->GetFileTransferer().StartTask(CNetFileTransferer::RequestType::REJOIN,
		[&server, newHostID](std::string buffer)
		{
			// We've received the game state from an existing player - now we need to send it onwards
			// to the newly rejoining player.

			const auto sessionIt = std::find_if(server.m_Sessions.begin(), server.m_Sessions.end(),
				[newHostID](const auto& serverSession)
				{
					return serverSession->GetHostID() == newHostID;
				});

			if (sessionIt == server.m_Sessions.end())
			{
				LOGMESSAGE("Net server: rejoining client disconnected before we sent to it");
				return;
			}

			// Store the received state file, and tell the client to stant downloading it from us.
			// TODO: The server will get kind of confused if there's multiple clients downloading in
			// parallel; they'll race and get whichever happens to be the latest received by the
			// server, which should still work but isn't great.
			server.m_JoinSyncFile = std::move(buffer);

			// Send the init attributes alongside - these should be correct since the game should be
			// started.
			CJoinSyncStartMessage message;
			message.m_InitAttributes = Script::StringifyJSON(
				Script::Request{server.GetScriptInterface()}, &server.m_InitAttributes);
			(*sessionIt)->SendMessage(&message);
		});

	session->SetNextState(NSS_JOIN_SYNCING);
	return true;
}

bool CNetServerWorker::OnSimulationCommand(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_SIMULATION_COMMAND));

	CNetServerWorker& server = session->GetServer();

	CSimulationMessage* message = (CSimulationMessage*)event->GetParamRef();

	// Ignore messages sent by one player on behalf of another player
	// unless cheating is enabled
	bool cheatsEnabled = false;
	const Script::Interface& scriptInterface = server.GetScriptInterface();
	Script::Request rq(scriptInterface);
	JS::RootedValue settings(rq.cx);
	Script::GetProperty(rq, server.m_InitAttributes, "settings", &settings);
	if (Script::HasProperty(rq, settings, "CheatsEnabled"))
		Script::GetProperty(rq, settings, "CheatsEnabled", cheatsEnabled);

	PlayerAssignmentMap::iterator it = server.m_PlayerAssignments.find(session->GetGUID());
	// When cheating is disabled, fail if the player the message claims to
	// represent does not exist or does not match the sender's player name
	if (!cheatsEnabled && (it == server.m_PlayerAssignments.end() || it->second.m_PlayerID != message->m_Player))
		return true;

	// Send it back to all clients that have finished
	// the loading screen (and the synchronization when rejoining)
	server.Multicast(message, { NSS_INGAME });

	// Save all the received commands
	if (server.m_SavedCommands.size() < static_cast<size_t>(message->m_Turn) + 1)
		server.m_SavedCommands.resize(static_cast<size_t>(message->m_Turn) + 1);
	server.m_SavedCommands[static_cast<size_t>(message->m_Turn)].push_back(*message);

	// TODO: we shouldn't send the message back to the client that first sent it
	return true;
}

bool CNetServerWorker::OnFlare(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_FLARE));

	CNetServerWorker& server = session->GetServer();
	CFlareMessage* message = (CFlareMessage*)event->GetParamRef();
	message->m_GUID = session->GetGUID();
	server.Multicast(message, { NSS_INGAME });

	return true;
}

bool CNetServerWorker::OnSyncCheck(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_SYNC_CHECK));

	CNetServerWorker& server = session->GetServer();

	CSyncCheckMessage* message = (CSyncCheckMessage*)event->GetParamRef();

	server.m_ServerTurnManager->NotifyFinishedClientUpdate(*session, message->m_Turn, message->m_Hash);
	return true;
}

bool CNetServerWorker::OnEndCommandBatch(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_END_COMMAND_BATCH));

	CNetServerWorker& server = session->GetServer();

	CEndCommandBatchMessage* message = (CEndCommandBatchMessage*)event->GetParamRef();

	// The turn-length field is ignored
	server.m_ServerTurnManager->NotifyFinishedClientCommands(*session, message->m_Turn);
	return true;
}

bool CNetServerWorker::OnChat(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_CHAT));

	CNetServerWorker& server = session->GetServer();

	CChatMessage* message = (CChatMessage*)event->GetParamRef();

	message->m_SenderGUID = session->GetGUID();

	const std::vector<NetServerSessionState> receivingStates{NSS_PREGAME, NSS_INGAME};
	const std::vector messageReceivers{std::exchange(message->m_Receivers, {})};

	if (messageReceivers.empty())
		server.Multicast(message, receivingStates);
	else
	{
		auto receivers = std::make_optional<std::vector<std::string>>();
		std::transform(messageReceivers.begin(), messageReceivers.end(), std::back_inserter(*receivers),
			std::mem_fn(&CChatMessage::S_m_Receivers::m_ReceiverGUID));
		server.Multicast(message, receivingStates, std::move(receivers));
	}

	return true;
}

bool CNetServerWorker::OnReady(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_READY));

	CNetServerWorker& server = session->GetServer();

	// Occurs if a client presses not-ready
	// in the very last moment before the hosts starts the game
	if (server.m_State == SERVER_STATE_LOADING)
		return true;

	CReadyMessage* message = (CReadyMessage*)event->GetParamRef();
	message->m_GUID = session->GetGUID();
	server.Multicast(message, { NSS_PREGAME });

	server.m_PlayerAssignments[message->m_GUID].m_Status = message->m_Status;

	return true;
}

bool CNetServerWorker::OnClearAllReady(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_CLEAR_ALL_READY));

	CNetServerWorker& server = session->GetServer();

	if (session->GetGUID() == server.m_ControllerGUID)
		server.ClearAllPlayerReady();

	return true;
}

bool CNetServerWorker::OnGameSetup(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_GAME_SETUP));

	CNetServerWorker& server = session->GetServer();

	// Changing the settings after gamestart is not implemented and would cause an Out-of-sync error.
	// This happened when doubleclicking on the startgame button.
	if (server.m_State != SERVER_STATE_PREGAME)
		return true;

	// Only the controller is allowed to send game setup updates.
	// TODO: it would be good to allow other players to request changes to some settings,
	// e.g. their civilisation.
	// Possibly this should use another message, to enforce a single source of truth.
	if (session->GetGUID() == server.m_ControllerGUID)
	{
		CGameSetupMessage* message = (CGameSetupMessage*)event->GetParamRef();
		server.Multicast(message, { NSS_PREGAME });
	}
	return true;
}

bool CNetServerWorker::OnAssignPlayer(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_ASSIGN_PLAYER));
	CNetServerWorker& server = session->GetServer();

	if (session->GetGUID() == server.m_ControllerGUID)
	{
		CAssignPlayerMessage* message = (CAssignPlayerMessage*)event->GetParamRef();
		server.AssignPlayer(message->m_PlayerID, message->m_GUID);
	}
	return true;
}

bool CNetServerWorker::OnGameStart(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_GAME_START));
	CNetServerWorker& server = session->GetServer();

	if (session->GetGUID() != server.m_ControllerGUID)
		return true;

	CGameStartMessage* message = (CGameStartMessage*)event->GetParamRef();
	server.StartGame(message->m_InitAttributes);
	return true;
}

bool CNetServerWorker::OnSavedGameStart(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_SAVED_GAME_START));
	CNetServerWorker& server{session->GetServer()};

	if (session->GetGUID() != server.m_ControllerGUID)
		return true;

	CGameSavedStartMessage* message = static_cast<CGameSavedStartMessage*>(event->GetParamRef());
	session->GetFileTransferer().StartTask(CNetFileTransferer::RequestType::LOADGAME,
		[&server, initAttributes = std::move(message->m_InitAttributes)](std::string buffer)
		{
			server.m_SavedState = std::move(buffer);

			server.StartSavedGame(initAttributes);
		});
	return true;
}

bool CNetServerWorker::OnLoadedGame(CNetServerSession* loadedSession, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_LOADED_GAME));

	CNetServerWorker& server = loadedSession->GetServer();

	// We're in the loading state, so wait until every client has loaded
	// before starting the game
	ENSURE(server.m_State == SERVER_STATE_LOADING);
	if (server.CheckGameLoadStatus(loadedSession))
		return true;

	CClientsLoadingMessage message;
	// We always send all GUIDs of clients in the loading state
	// so that we don't have to bother about switching GUI pages
	for (const auto& session : server.m_Sessions)
		if (session->GetCurrState() != NSS_INGAME && loadedSession->GetGUID() != session->GetGUID())
		{
			CClientsLoadingMessage::S_m_Clients client;
			client.m_GUID = session->GetGUID();
			message.m_Clients.push_back(client);
		}

	// If no other player is loading the server can clear the savestate
	if (message.m_Clients.empty())
		server.m_SavedState.clear();

	// Send to the client who has loaded the game but did not reach the NSS_INGAME state yet
	loadedSession->SendMessage(&message);
	server.Multicast(&message, { NSS_INGAME });

	return true;
}

bool CNetServerWorker::OnJoinSyncingLoadedGame(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	// A client rejoining an in-progress game has now finished loading the
	// map and deserialized the initial state.
	// The simulation may have progressed since then, so send any subsequent
	// commands to them and set them as an active player so they can participate
	// in all future turns.
	//
	// (TODO: if it takes a long time for them to receive and execute all these
	// commands, the other players will get frozen for that time and may be unhappy;
	// we could try repeating this process a few times until the client converges
	// on the up-to-date state, before setting them as active.)

	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_LOADED_GAME));

	CNetServerWorker& server = session->GetServer();

	CLoadedGameMessage* message = (CLoadedGameMessage*)event->GetParamRef();

	turn_id_t turn = message->m_CurrentTurn;
	turn_id_t readyTurn = server.m_ServerTurnManager->GetReadyTurn();

	// Send them all commands received since their saved state,
	// and turn-ended messages for any turns that have already been processed
	for (turn_id_t i = turn + 1; i < std::max(readyTurn + 1, static_cast<turn_id_t>(server.m_SavedCommands.size())); ++i)
	{
		if (static_cast<size_t>(i) < server.m_SavedCommands.size())
			for (size_t j = 0; j < server.m_SavedCommands[i].size(); ++j)
				session->SendMessage(&server.m_SavedCommands[i][j]);

		if (i <= readyTurn)
		{
			CEndCommandBatchMessage endMessage;
			endMessage.m_Turn = i;
			endMessage.m_TurnLength = server.m_ServerTurnManager->GetSavedTurnLength(i);
			session->SendMessage(&endMessage);
		}
	}

	// Tell the turn manager to expect commands from this new client
	// Special case: the controller shouldn't be treated as an observer in any case.
	bool isObserver = server.m_PlayerAssignments[session->GetGUID()].m_PlayerID == -1 && server.m_ControllerGUID != session->GetGUID();
	server.m_ServerTurnManager->InitialiseClient(session->GetHostID(), readyTurn, isObserver);

	// Tell the client that everything has finished loading and it should start now
	CLoadedGameMessage loaded;
	loaded.m_CurrentTurn = readyTurn;
	session->SendMessage(&loaded);

	return true;
}

bool CNetServerWorker::OnRejoined(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	// A client has finished rejoining and the loading screen disappeared.
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_REJOINED));

	CNetServerWorker& server = session->GetServer();

	// Inform everyone of the client having rejoined
	CRejoinedMessage* message = (CRejoinedMessage*)event->GetParamRef();
	message->m_GUID = session->GetGUID();
	server.Multicast(message, { NSS_INGAME });

	// Send all pausing players to the rejoined client.
	for (const CStr& guid : server.m_PausingPlayers)
	{
		CClientPausedMessage pausedMessage;
		pausedMessage.m_GUID = guid;
		pausedMessage.m_Pause = true;
		session->SendMessage(&pausedMessage);
	}

	return true;
}

bool CNetServerWorker::OnKickPlayer(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_KICKED));

	CNetServerWorker& server = session->GetServer();

	if (session->GetGUID() == server.m_ControllerGUID)
	{
		CKickedMessage* message = (CKickedMessage*)event->GetParamRef();
		server.KickPlayer(message->m_Name, message->m_Ban);
	}
	return true;
}

bool CNetServerWorker::OnDisconnect(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_CONNECTION_LOST));

	CNetServerWorker& server = session->GetServer();

	server.OnUserLeave(session);

	return true;
}

bool CNetServerWorker::OnClientPaused(CNetServerSession* session, CFsmEvent<CNetMessage*>* event)
{
	ENSURE(event->GetType() == static_cast<unsigned int>(NMT_CLIENT_PAUSED));

	CNetServerWorker& server = session->GetServer();

	CClientPausedMessage* message = (CClientPausedMessage*)event->GetParamRef();

	message->m_GUID = session->GetGUID();

	// Update the list of pausing players.
	std::vector<CStr>::iterator player = std::find(server.m_PausingPlayers.begin(), server.m_PausingPlayers.end(), session->GetGUID());

	if (message->m_Pause)
	{
		if (player != server.m_PausingPlayers.end())
			return true;

		server.m_PausingPlayers.push_back(session->GetGUID());
	}
	else
	{
		if (player == server.m_PausingPlayers.end())
			return true;

		server.m_PausingPlayers.erase(player);
	}

	// Send messages to clients that are in game, and are not the client who paused.
	for (const auto& netSession : server.m_Sessions)
		if (netSession->GetCurrState() == NSS_INGAME && message->m_GUID != netSession->GetGUID())
			netSession->SendMessage(message);

	return true;
}

bool CNetServerWorker::CheckGameLoadStatus(CNetServerSession* changedSession)
{
	for (const auto& session : m_Sessions)
		if (session.get() != changedSession && session->GetCurrState() != NSS_INGAME)
			return false;

	// Inform clients that everyone has loaded the map and that the game can start
	CLoadedGameMessage loaded;
	loaded.m_CurrentTurn = 0;

	// Notice the changedSession is still in the NSS_PREGAME state
	Multicast(&loaded, { NSS_PREGAME, NSS_INGAME });

	m_State = SERVER_STATE_INGAME;
	return true;
}

void CNetServerWorker::PreStartGame(const CStr& initAttribs)
{
	for (std::pair<const CStr, PlayerAssignment>& player : m_PlayerAssignments)
		if (player.second.m_Enabled && player.second.m_PlayerID != -1 && player.second.m_Status == 0)
		{
			LOGERROR("Tried to start the game without player \"{}\" being ready!",
				utf8_from_wstring(player.second.m_Name).c_str());
			return;
		}

	m_ServerTurnManager = std::make_unique<CNetServerTurnManager>(*this);

	for (const auto& session : m_Sessions)
	{
		// InitialiseClient makes the NetServerTurnManager wait for this client.
		// But we only want to wait for clients who have passed authentication, i.e. who have the correct password.
		// Therefore we may not call InitialiseClient for unauthenticated clients.
		if (session->GetCurrState() != NSS_PREGAME)
		{
			// We only support clients joining before or after, not during the loading screen.
			// Therefore we have to disconnect clients who did not complete the authentication yet.
			LOGMESSAGE("Dropping client ({} / {} / {}) not in NSS_PREGAME when starting the game",
				session->GetUserName().ToUTF8(), session->GetGUID(), session->GetHostID());
			session->Disconnect(NDR_SERVER_LOADING);
			continue;
		}

		// Special case: the controller shouldn't be treated as an observer in any case.
		bool isObserver = m_PlayerAssignments[session->GetGUID()].m_PlayerID == -1 && m_ControllerGUID != session->GetGUID();
		m_ServerTurnManager->InitialiseClient(session->GetHostID(), 0, isObserver);
	}

	m_State = SERVER_STATE_LOADING;

	// Remove players and observers that are not present when the game starts
	for (PlayerAssignmentMap::iterator it = m_PlayerAssignments.begin(); it != m_PlayerAssignments.end();)
		if (it->second.m_Enabled)
			++it;
		else
			it = m_PlayerAssignments.erase(it);

	SendPlayerAssignments();

	// Update init attributes. They should no longer change.
	Script::ParseJSON(Script::Request(m_ScriptInterface), initAttribs, &m_InitAttributes);
}

void CNetServerWorker::StartGame(const CStr& initAttribs)
{
	PreStartGame(initAttribs);

	CGameStartMessage gameStart;
	gameStart.m_InitAttributes = initAttribs;
	Multicast(&gameStart, { NSS_PREGAME });
}

void CNetServerWorker::StartSavedGame(const CStr& initAttribs)
{
	PreStartGame(initAttribs);

	CGameSavedStartMessage gameSavedStart;
	gameSavedStart.m_InitAttributes = initAttribs;
	Multicast(&gameSavedStart, { NSS_PREGAME });
}

CStrW CNetServerWorker::SanitisePlayerName(const CStrW& original)
{
	const size_t MAX_LENGTH = 32;

	CStrW name = original;
	name.Replace(L"[", L"{"); // remove GUI tags
	name.Replace(L"]", L"}"); // remove for symmetry

	// Restrict the length
	if (name.length() > MAX_LENGTH)
		name = name.substr(0, MAX_LENGTH);

	// Don't allow surrounding whitespace
	name.Trim(PS_TRIM_BOTH);

	// Don't allow empty name
	if (name.empty())
		name = L"Anonymous";

	return name;
}

CStrW CNetServerWorker::DeduplicatePlayerName(const CStrW& original)
{
	CStrW name = original;

	// Try names "Foo", "Foo (2)", "Foo (3)", etc
	size_t id = 2;
	while (true)
	{
		bool unique = true;
		for (const auto& session : m_Sessions)
		{
			if (session->GetUserName() == name)
			{
				unique = false;
				break;
			}
		}

		if (unique)
			return name;

		name = fmt::format(L"{} ({})", original, id++);
	}
}

void CNetServerWorker::SendHolePunchingMessage(const CStr& /*ipStr*/, std::uint16_t /*port*/)
{
}




CNetServer::CNetServer(const bool continueSavedGame, std::uint16_t port, const bool useLobbyAuth,
	std::string password, std::string controllerSecret, std::string initAttributes) :
	m_Worker{continueSavedGame, port, useLobbyAuth, password, std::move(controllerSecret),
		std::move(initAttributes)},
	m_LobbyAuth{useLobbyAuth},
	m_Password{std::move(password)}
{
	if (!useLobbyAuth)
		return;

	// In lobby, we send our public ip and port on request to the players who want to connect.
	// Thus we need to know our public IP and use STUN to get it.
	std::lock_guard<std::mutex> lock(m_Worker.m_WorkerMutex);
	if (!StunClient::FindPublicIP(m_Worker.m_SocketDescriptor, m_PublicIp, m_PublicPort))
		throw std::runtime_error{"Failed to resolve public IP-address."};
}

bool CNetServer::UseLobbyAuth() const
{
	return m_LobbyAuth;
}

CStr CNetServer::GetPublicIp() const
{
	return m_PublicIp;
}

std::uint16_t CNetServer::GetPublicPort() const
{
	return m_PublicPort;
}

std::uint16_t CNetServer::GetLocalPort() const
{
	return m_Worker.m_LocalPort;
}

std::string CNetServer::GetCertificateFingerprint() const
{
	return m_Worker.m_CertificateFingerprint;
}

bool CNetServer::CheckPasswordAndIncrement(const std::string& username, const std::string& password, const std::string& salt)
{
	std::unordered_map<std::string, int>::iterator it = m_FailedAttempts.find(username);
	if (m_Worker.CheckPassword(password, salt))
	{
		if (it != m_FailedAttempts.end())
			it->second = 0;
		return true;
	}
	if (it == m_FailedAttempts.end())
		m_FailedAttempts.emplace(username, 1);
	else
		it->second++;
	return false;
}

bool CNetServer::IsBanned(const std::string& username) const
{
	std::unordered_map<std::string, int>::const_iterator it = m_FailedAttempts.find(username);
	return it != m_FailedAttempts.end() && it->second >= FAILED_PASSWORD_TRIES_BEFORE_BAN;
}

void CNetServer::StartGame()
{
	std::lock_guard<std::mutex> lock(m_Worker.m_WorkerMutex);
	m_Worker.m_StartGameQueue.push_back(true);
}

void CNetServer::OnLobbyAuth(const CStr& name, const CStr& token)
{
	std::lock_guard<std::mutex> lock(m_Worker.m_WorkerMutex);
	m_Worker.m_LobbyAuthQueue.push_back(std::make_pair(name, token));
}

void CNetServer::SetTurnLength(std::uint32_t msecs)
{
	std::lock_guard<std::mutex> lock(m_Worker.m_WorkerMutex);
	m_Worker.m_TurnLengthQueue.push_back(msecs);
}

void CNetServer::SendHolePunchingMessage(const CStr& ip, std::uint16_t port)
{
	m_Worker.SendHolePunchingMessage(ip, port);
}

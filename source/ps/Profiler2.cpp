/* Copyright (C) 2026 Wildfire Games.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "precompiled.h"

#include "Profiler2.h"

#include "lib/code_generation.h"
#include "lib/os_path.h"
#include "lib/path.h"
#include "network/HttpServer.h"
#include "ps/CLogger.h"
#include "ps/CStr.h"
#include "ps/ConfigDB.h"
#include "ps/Future.h"
#include "ps/Profiler2GPU.h"
#include "ps/Pyrogenesis.h"
#include "ps/TaskManager.h"

#include <algorithm>
#include <cstdio>
#include <fmt/format.h>
#include <fstream>
#include <functional>
#include <httplib.h>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <variant>

CProfiler2 g_Profiler2;

const size_t CProfiler2::MAX_ATTRIBUTE_LENGTH = 256;

// TODO: what's a good size?
const size_t CProfiler2::BUFFER_SIZE = 4 * 1024 * 1024;

// A human-recognisable pattern (for debugging) followed by random bytes (for uniqueness)
const u8 CProfiler2::RESYNC_MAGIC[8] = {0x11, 0x22, 0x33, 0x44, 0xf4, 0x93, 0xbe, 0x15};

thread_local CProfiler2::ThreadStorage* CProfiler2::m_CurrentStorage = nullptr;

CProfiler2::CProfiler2() :
	m_Initialised{false}, m_FrameNumber{0}, m_HttpServer{nullptr}, m_HttpServerThread{}, m_GPU{nullptr}
{
}

CProfiler2::~CProfiler2()
{
	if (m_Initialised)
		Shutdown();
}

void CProfiler2::Initialise()
{
	ENSURE(!m_Initialised);
	m_Initialised = true;

	RegisterCurrentThread("main");
}

void CProfiler2::InitialiseGPU()
{
	ENSURE(!m_GPU);
	m_GPU = new CProfiler2GPU(*this);
}

void CProfiler2::EnableHTTP()
{
	ENSURE(m_Initialised);
	LOGMESSAGERENDER("Starting profiler2 HTTP server");

	std::lock_guard lock{m_Mutex};

	// Ignore multiple enablings
	if (m_HttpServer)
		return;

	m_HttpServer = PS::Net::createHttpServer();

	m_HttpServer->Get("/download", [this](const httplib::Request &, httplib::Response &) {
		SaveToFile();
	});

	m_HttpServer->Get("/overview", [this](const httplib::Request &, httplib::Response &res) {
		std::stringstream stream;
		ConstructJSONOverview(stream);
		res.set_content(stream.str(), "application/json");
	});

	m_HttpServer->Get("/query", [this](const httplib::Request &req, httplib::Response &res) {
		if (!req.has_param("thread"))
		{
			res.set_content("Request \"query\" needs parameter \"thread\"", "text/plain");
			res.status = httplib::StatusCode::BadRequest_400;
			return;
		}

		std::string thread = req.get_param_value("thread");
		std::stringstream stream;
		ConstructJSONResponse(stream, thread);
		res.set_content(stream.str(), "application/json");
	});

	m_HttpServer->set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
		// TODO: Not ideal for security reasons
		res.set_header("Access-Control-Allow-Origin", "*");
	});

	m_HttpServerThread = std::thread([this](){
		using namespace std::literals;
		const int listeningPort{CConfigDB::GetIfInitialised("profiler2.server.port", 8000)};
		const std::string listeningServer{CConfigDB::GetIfInitialised("profiler2.server", "127.0.0.1"s)};

		if (!m_HttpServer->listen(listeningServer, listeningPort))
		{
			LOGERROR("Failed to start http server");
		}
	});
}

void CProfiler2::EnableGPU()
{
	ENSURE(m_Initialised);
	if (!m_GPU)
	{
		LOGMESSAGERENDER("Starting profiler2 GPU mode");
		InitialiseGPU();
	}
}

void CProfiler2::ShutdownGPU()
{
	LOGMESSAGERENDER("Shutting down profiler2 GPU mode");
	SAFE_DELETE(m_GPU);
}

void CProfiler2::ShutDownHTTP()
{
	LOGMESSAGERENDER("Shutting down profiler2 HTTP server");
	std::lock_guard lock{m_Mutex};

	if (!m_HttpServer)
		return;

	m_HttpServer->stop();
	if(m_HttpServerThread.joinable())
		m_HttpServerThread.join();
	m_HttpServer.reset();
}

void CProfiler2::Toggle()
{
	LOGMESSAGERENDER("Toggle profiler http");
	if (m_GPU && m_HttpServer)
	{
		ShutdownGPU();
		ShutDownHTTP();
	}
	else if (!m_GPU && !m_HttpServer)
	{
		EnableGPU();
		EnableHTTP();
	}
	else
	{
		LOGMESSAGERENDER("Toggle profile bad state!");
	}
}

void CProfiler2::Shutdown()
{
	ENSURE(m_Initialised);

	ENSURE(!m_GPU); // must shutdown GPU before profiler
	ENSURE(!m_HttpServer); // must shutdown HTTP server before profiler

	// the destructor is not called for the main thread
	// we have to call it manually to avoid memory leaks
	ENSURE(Threading::IsMainThread());
	m_Initialised = false;
}

void CProfiler2::RecordGPUFrameStart(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	if (m_GPU)
		m_GPU->FrameStart(deviceCommandContext);
}

void CProfiler2::RecordGPUFrameEnd(Renderer::Backend::IDeviceCommandContext* deviceCommandContext)
{
	if (m_GPU)
		m_GPU->FrameEnd(deviceCommandContext);
}

void CProfiler2::RecordGPURegionEnter(Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id)
{
	if (m_GPU)
		m_GPU->RegionEnter(deviceCommandContext, id);
}

void CProfiler2::RecordGPURegionLeave(Renderer::Backend::IDeviceCommandContext* deviceCommandContext, const char* id)
{
	if (m_GPU)
		m_GPU->RegionLeave(deviceCommandContext, id);
}

void CProfiler2::RegisterCurrentThread(const std::string& name)
{
	ENSURE(m_Initialised);

	// Must not register a thread more than once.
	ENSURE(m_CurrentStorage == nullptr);

	m_CurrentStorage = new ThreadStorage(*this, name);
	AddThreadStorage(m_CurrentStorage);

	RecordSyncMarker();
	RecordEvent("thread start");
}

void CProfiler2::AddThreadStorage(ThreadStorage* storage)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Threads.push_back(std::unique_ptr<ThreadStorage>(storage));
}

void CProfiler2::RemoveThreadStorage(ThreadStorage* storage)
{
	std::lock_guard<std::mutex> lock(m_Mutex);
	m_Threads.erase(std::find_if(m_Threads.begin(), m_Threads.end(), [storage](const std::unique_ptr<ThreadStorage>& s) { return s.get() == storage; }));
}

CProfiler2::ThreadStorage::ThreadStorage(CProfiler2& profiler, const std::string& name) :
m_Profiler(profiler), m_Name(name), m_BufferPos0(0), m_BufferPos1(0), m_LastTime(timer_Time())
{
	m_Buffer = new u8[BUFFER_SIZE];
	memset(m_Buffer, ITEM_NOP, BUFFER_SIZE);
}

CProfiler2::ThreadStorage::~ThreadStorage()
{
	delete[] m_Buffer;
}

void CProfiler2::ThreadStorage::Write(EItem type, const void* item, u32 itemSize)
{
	// See m_BufferPos0 etc for comments on synchronisation

	u32 size = 1 + itemSize;
	u32 start = m_BufferPos0;
	if (start + size > BUFFER_SIZE)
	{
		// The remainder of the buffer is too small - fill the rest
		// with NOPs then start from offset 0, so we don't have to
		// bother splitting the real item across the end of the buffer

		m_BufferPos0 = size;
		COMPILER_FENCE; // must write m_BufferPos0 before m_Buffer

		memset(m_Buffer + start, 0, BUFFER_SIZE - start);
		start = 0;
	}
	else
	{
		m_BufferPos0 = start + size;
		COMPILER_FENCE; // must write m_BufferPos0 before m_Buffer
	}

	m_Buffer[start] = (u8)type;
	memcpy(&m_Buffer[start + 1], item, itemSize);

	COMPILER_FENCE; // must write m_BufferPos1 after m_Buffer
	m_BufferPos1 = start + size;
}

std::string CProfiler2::ThreadStorage::GetBuffer()
{
	// Called from an arbitrary thread (not the one writing to the buffer).
	//
	// See comments on m_BufferPos0 etc.

	std::unique_ptr<u8[]> buffer{std::make_unique<u8[]>(BUFFER_SIZE)};

	u32 pos1 = m_BufferPos1;
	COMPILER_FENCE; // must read m_BufferPos1 before m_Buffer

	memcpy(buffer.get(), m_Buffer, BUFFER_SIZE);

	COMPILER_FENCE; // must read m_BufferPos0 after m_Buffer
	u32 pos0 = m_BufferPos0;

	// The range [pos1, pos0) modulo BUFFER_SIZE is invalid, so concatenate the rest of the buffer

	if (pos1 <= pos0) // invalid range is in the middle of the buffer
		return std::string(buffer.get()+pos0, buffer.get()+BUFFER_SIZE) + std::string(buffer.get(), buffer.get()+pos1);
	else // invalid wrap is wrapped around the end/start buffer
		return std::string(buffer.get()+pos0, buffer.get()+pos1);
}

void CProfiler2::ThreadStorage::RecordAttribute(const char* fmt, va_list argp)
{
	char buffer[MAX_ATTRIBUTE_LENGTH + 4] = {0}; // first 4 bytes are used for storing length
	int len = vsnprintf(buffer + 4, MAX_ATTRIBUTE_LENGTH - 1, fmt, argp); // subtract 1 from length to make MSVC vsnprintf safe
	// (Don't use vsprintf_s because it treats overflow as fatal)

	// Terminate the string if the printing was truncated
	if (len < 0 || len >= (int)MAX_ATTRIBUTE_LENGTH - 1)
	{
		strncpy(buffer + 4 + MAX_ATTRIBUTE_LENGTH - 4, "...", 4);
		len = MAX_ATTRIBUTE_LENGTH - 1; // excluding null terminator
	}

	// Store the length in the buffer
	memcpy(buffer, &len, sizeof(len));

	Write(ITEM_ATTRIBUTE, buffer, 4 + len);
}

// this flattens the stack, use it sensibly
void rewriteBuffer(u8* buffer, u32& bufferSize)
{
	double startTime = timer_Time();

	u32 size = bufferSize;
	u32 readPos = 0;

	double initialTime = -1;
	double total_time = -1;
	const char* regionName;
	std::set<std::string> topLevelArgs;

	using infoPerType = std::tuple<const char*, double, std::set<std::string> >;
	using timeByTypeMap = std::unordered_map<std::string, infoPerType>;

	timeByTypeMap timeByType;
	std::vector<double> last_time_stack;
	std::vector<const char*> last_names;

	// never too many hacks
	std::string current_attribute = "";
	std::map<std::string, double> time_per_attribute;

	// Let's read the first event
	{
		u8 type = buffer[readPos];
		++readPos;
		if (type != CProfiler2::ITEM_ENTER)
		{
			debug_warn("Profiler2: Condensing a region should run into ITEM_ENTER first");
			return; // do nothing
		}
		CProfiler2::SItem_dt_id item;
		memcpy(&item, buffer + readPos, sizeof(item));
		readPos += sizeof(item);

		regionName = item.id;
		last_names.push_back(item.id);
		initialTime = (double)item.dt;
	}

	// Read subsequent events. Flatten hierarchy because it would get too complicated otherwise.
	// To make sure time doesn't bloat, subtract time from nested events
	while (readPos < size)
	{
		u8 type = buffer[readPos];
		++readPos;

		switch (type)
		{
		case CProfiler2::ITEM_NOP:
		{
			// ignore
			break;
		}
		case CProfiler2::ITEM_SYNC:
		{
			debug_warn("Aggregated regions should not be used across frames");
			// still try to act sane
			readPos += sizeof(double);
			readPos += sizeof(CProfiler2::RESYNC_MAGIC);
			break;
		}
		case CProfiler2::ITEM_EVENT:
		{
			// skip for now
			readPos += sizeof(CProfiler2::SItem_dt_id);
			break;
		}
		case CProfiler2::ITEM_ENTER:
		{
			CProfiler2::SItem_dt_id item;
			memcpy(&item, buffer + readPos, sizeof(item));
			readPos += sizeof(item);
			last_time_stack.push_back((double)item.dt);
			last_names.push_back(item.id);
			current_attribute = "";
			break;
		}
		case CProfiler2::ITEM_LEAVE:
		{
			float item_time;
			memcpy(&item_time, buffer + readPos, sizeof(float));
			readPos += sizeof(float);

			if (last_names.empty())
			{
				// we somehow lost the first entry in the process
				debug_warn("Invalid buffer for condensing");
			}
			const char* item_name = last_names.back();
			last_names.pop_back();

			if (last_time_stack.empty())
			{
				// this is the leave for the whole scope
				total_time = (double)item_time;
				break;
			}
			double time = (double)item_time - last_time_stack.back();

			std::string name = std::string(item_name);
			timeByTypeMap::iterator TimeForType = timeByType.find(name);
			if (TimeForType == timeByType.end())
			{
				// keep reference to the original char pointer to make sure we don't break things down the line
				std::get<0>(timeByType[name]) = item_name;
				std::get<1>(timeByType[name]) = 0;
			}
			std::get<1>(timeByType[name]) += time;

			last_time_stack.pop_back();
			// if we were nested, subtract our time from the below scope by making it look like it starts later
			if (!last_time_stack.empty())
				last_time_stack.back() += time;

			if (!current_attribute.empty())
			{
				time_per_attribute[current_attribute] += time;
			}

			break;
		}
		case CProfiler2::ITEM_ATTRIBUTE:
		{
			// skip for now
			u32 len;
			memcpy(&len, buffer + readPos, sizeof(len));
			ENSURE(len <= CProfiler2::MAX_ATTRIBUTE_LENGTH);
			readPos += sizeof(len);

			char message[CProfiler2::MAX_ATTRIBUTE_LENGTH] = {0};
			memcpy(&message[0], buffer + readPos, std::min(size_t(len), CProfiler2::MAX_ATTRIBUTE_LENGTH));
			CStr mess = CStr((const char*)message, len);
			if (!last_names.empty())
			{
				timeByTypeMap::iterator it = timeByType.find(std::string(last_names.back()));
				if (it == timeByType.end())
					topLevelArgs.insert(mess);
				else
					std::get<2>(timeByType[std::string(last_names.back())]).insert(mess);
			}
			readPos += len;
			current_attribute = mess;
			break;
		}
		default:
			debug_warn(L"Invalid profiler item when condensing buffer");
			continue;
		}
	}

	// rewrite the buffer
	// what we rewrite will always be smaller than the current buffer's size
	u32 writePos = 0;
	double curTime = initialTime;
	// the region enter
	{
		CProfiler2::SItem_dt_id item = { (float)curTime, regionName };
		buffer[writePos] = (u8)CProfiler2::ITEM_ENTER;
		memcpy(buffer + writePos + 1, &item, sizeof(item));
		writePos += sizeof(item) + 1;
		// add a nanosecond for sanity
		curTime += 0.000001;
	}
	// sub-events, aggregated
	for (const std::pair<const std::string, infoPerType>& type : timeByType)
	{
		CProfiler2::SItem_dt_id item = { (float)curTime, std::get<0>(type.second) };
		buffer[writePos] = (u8)CProfiler2::ITEM_ENTER;
		memcpy(buffer + writePos + 1, &item, sizeof(item));
		writePos += sizeof(item) + 1;

		// write relevant attributes if present
		for (const std::string& attrib : std::get<2>(type.second))
		{
			buffer[writePos] = (u8)CProfiler2::ITEM_ATTRIBUTE;
			writePos++;
			std::string basic = attrib;
			std::map<std::string, double>::iterator time_attrib = time_per_attribute.find(attrib);
			if (time_attrib != time_per_attribute.end())
				basic += " " + CStr::FromInt(1000000*time_attrib->second) + "us";

			u32 length = static_cast<u32>(basic.size());
			memcpy(buffer + writePos, &length, sizeof(length));
			writePos += sizeof(length);
			memcpy(buffer + writePos, basic.c_str(), length);
			writePos += length;
		}

		curTime += std::get<1>(type.second);

		float leave_time = (float)curTime;
		buffer[writePos] = (u8)CProfiler2::ITEM_LEAVE;
		memcpy(buffer + writePos + 1, &leave_time, sizeof(float));
		writePos += sizeof(float) + 1;
	}
	// Time of computation
	{
		CProfiler2::SItem_dt_id item = { (float)curTime, "CondenseBuffer" };
		buffer[writePos] = (u8)CProfiler2::ITEM_ENTER;
		memcpy(buffer + writePos + 1, &item, sizeof(item));
		writePos += sizeof(item) + 1;
	}
	{
		float time_out = (float)(curTime + timer_Time() - startTime);
		buffer[writePos] = (u8)CProfiler2::ITEM_LEAVE;
		memcpy(buffer + writePos + 1, &time_out, sizeof(float));
		writePos += sizeof(float) + 1;
		// add a nanosecond for sanity
		curTime += 0.000001;
	}

	// the region leave
	{
		if (total_time < 0)
		{
			total_time = curTime + 0.000001;

			buffer[writePos] = (u8)CProfiler2::ITEM_ATTRIBUTE;
			writePos++;
			u32 length = sizeof("buffer overflow");
			memcpy(buffer + writePos, &length, sizeof(length));
			writePos += sizeof(length);
			memcpy(buffer + writePos, "buffer overflow", length);
			writePos += length;
		}
		else if (total_time < curTime)
		{
			// this seems to happen on rare occasions.
			curTime = total_time;
		}
		float leave_time = (float)total_time;
		buffer[writePos] = (u8)CProfiler2::ITEM_LEAVE;
		memcpy(buffer + writePos + 1, &leave_time, sizeof(float));
		writePos += sizeof(float) + 1;
	}
	bufferSize = writePos;
}

void CProfiler2::ConstructJSONOverview(std::ostream& stream)
{
	PROFILE2("CProfiler2::ConstructJSONOverview");

	std::lock_guard<std::mutex> lock(m_Mutex);

	stream << "{\"threads\":[";
	bool first_time = true;
	for (std::unique_ptr<ThreadStorage>& storage : m_Threads)
	{
		if (!first_time)
			stream << ",";
		stream << "{\"name\":\"" << CStr(storage->GetName()).EscapeToPrintableASCII() << "\"}";
		first_time = false;
	}
	stream << "]}";
}

/**
 * Given a buffer and a visitor class (with functions OnEvent, OnEnter, OnLeave, OnAttribute),
 * calls the visitor for every item in the buffer.
 */
template<typename V>
void RunBufferVisitor(const std::string& buffer, V& visitor)
{
	PROFILE2("Profiler2 RunBufferVisitor");

	// The buffer doesn't necessarily start at the beginning of an item
	// (we just grabbed it from some arbitrary point in the middle),
	// so scan forwards until we find a sync marker.
	// (This is probably pretty inefficient.)

	u32 realStart = (u32)-1; // the start point decided by the scan algorithm

	for (u32 start = 0; start + 1 + sizeof(CProfiler2::RESYNC_MAGIC) <= buffer.length(); ++start)
	{
		if (buffer[start] == CProfiler2::ITEM_SYNC
			&& memcmp(buffer.c_str() + start + 1, &CProfiler2::RESYNC_MAGIC, sizeof(CProfiler2::RESYNC_MAGIC)) == 0)
		{
			realStart = start;
			break;
		}
	}

	ENSURE(realStart != (u32)-1); // we should have found a sync point somewhere in the buffer

	u32 pos = realStart; // the position as we step through the buffer

	double lastTime = -1;
		// set to non-negative by EVENT_SYNC; we ignore all items before that
		// since we can't compute their absolute times

	while (pos < buffer.length())
	{
		u8 type = buffer[pos];
		++pos;

		switch (type)
		{
		case CProfiler2::ITEM_NOP:
		{
			// ignore
			break;
		}
		case CProfiler2::ITEM_SYNC:
		{
			u8 magic[sizeof(CProfiler2::RESYNC_MAGIC)];
			double t;
			memcpy(magic, buffer.c_str()+pos, ARRAY_SIZE(magic));
			ENSURE(memcmp(magic, &CProfiler2::RESYNC_MAGIC, sizeof(CProfiler2::RESYNC_MAGIC)) == 0);
			pos += sizeof(CProfiler2::RESYNC_MAGIC);
			memcpy(&t, buffer.c_str()+pos, sizeof(t));
			pos += sizeof(t);
			lastTime = t;
			visitor.OnSync(lastTime);
			break;
		}
		case CProfiler2::ITEM_EVENT:
		{
			CProfiler2::SItem_dt_id item;
			memcpy(&item, buffer.c_str()+pos, sizeof(item));
			pos += sizeof(item);
			if (lastTime >= 0)
			{
				visitor.OnEvent(lastTime + (double)item.dt, item.id);
			}
			break;
		}
		case CProfiler2::ITEM_ENTER:
		{
			CProfiler2::SItem_dt_id item;
			memcpy(&item, buffer.c_str()+pos, sizeof(item));
			pos += sizeof(item);
			if (lastTime >= 0)
			{
				visitor.OnEnter(lastTime + (double)item.dt, item.id);
			}
			break;
		}
		case CProfiler2::ITEM_LEAVE:
		{
			float leave_time;
			memcpy(&leave_time, buffer.c_str() + pos, sizeof(float));
			pos += sizeof(float);
			if (lastTime >= 0)
			{
				visitor.OnLeave(lastTime + (double)leave_time);
			}
			break;
		}
		case CProfiler2::ITEM_ATTRIBUTE:
		{
			u32 len;
			memcpy(&len, buffer.c_str()+pos, sizeof(len));
			ENSURE(len <= CProfiler2::MAX_ATTRIBUTE_LENGTH);
			pos += sizeof(len);
			std::string attribute(buffer.c_str()+pos, buffer.c_str()+pos+len);
			pos += len;
			if (lastTime >= 0)
			{
				visitor.OnAttribute(attribute);
			}
			break;
		}
		default:
			debug_warn(L"Invalid profiler item when parsing buffer");
			return;
		}
	}
};

/**
 * Visitor class that dumps events as JSON.
 * TODO: this is pretty inefficient (in implementation and in output format).
 */
struct BufferVisitor_Dump
{
	NONCOPYABLE(BufferVisitor_Dump);
public:
	BufferVisitor_Dump(std::ostream& stream) : m_Stream(stream)
	{
	}

	void OnSync(double /*time*/)
	{
		// Split the array of items into an array of array (arbitrarily splitting
		// around the sync points) to avoid array-too-large errors in JSON decoders
		m_Stream << "null], [\n";
	}

	void OnEvent(double time, const char* id)
	{
		m_Stream << "[1," << std::fixed << std::setprecision(9) << time;
		m_Stream << ",\"" << CStr(id).EscapeToPrintableASCII() << "\"],\n";
	}

	void OnEnter(double time, const char* id)
	{
		m_Stream << "[2," << std::fixed << std::setprecision(9) << time;
		m_Stream << ",\"" << CStr(id).EscapeToPrintableASCII() << "\"],\n";
	}

	void OnLeave(double time)
	{
		m_Stream << "[3," << std::fixed << std::setprecision(9) << time << "],\n";
	}

	void OnAttribute(const std::string& attr)
	{
		m_Stream << "[4,\"" << CStr(attr).EscapeToPrintableASCII() << "\"],\n";
	}

	std::ostream& m_Stream;
};

const char* CProfiler2::ConstructJSONResponse(std::ostream& stream, const std::string& thread)
{
	PROFILE2("CProfiler2::ConstructJSONResponse");

	std::string buffer;

	{
		PROFILE2("Profiler2 get buffer");

		std::lock_guard<std::mutex> lock(m_Mutex); // lock against changes to m_Threads or deletions of ThreadStorage

		std::vector<std::unique_ptr<ThreadStorage>>::iterator it =
			std::find_if(m_Threads.begin(), m_Threads.end(), [&thread](std::unique_ptr<ThreadStorage>& storage) {
				return storage->GetName() == thread;
			});

		if (it == m_Threads.end())
			return "cannot find named thread";

		stream << "{\"events\":[\n";

		stream << "[\n";
		buffer = (*it)->GetBuffer();
	}

	BufferVisitor_Dump visitor(stream);
	RunBufferVisitor(buffer, visitor);

	stream << "null]\n]}";

	return NULL;
}

void CProfiler2::SaveToFile()
{
	OsPath path = psLogDir()/"profile2.jsonp";
	LOGMESSAGERENDER("Writing profile data to %s \n", path.string8().c_str());
	std::ofstream stream(OsString(path), std::ofstream::out | std::ofstream::trunc);
	ENSURE(stream.good());

	stream << "profileDataCB({\"threads\": [\n";
	bool first_time = true;
	for (std::unique_ptr<ThreadStorage>& storage : m_Threads)
	{
		if (!first_time)
			stream << ",\n";
		stream << "{\"name\":\"" << CStr(storage->GetName()).EscapeToPrintableASCII() << "\",\n";
		stream << "\"data\": ";
		ConstructJSONResponse(stream, storage->GetName());
		stream << "\n}";
		first_time = false;
	}
	stream << "\n]});\n";
}

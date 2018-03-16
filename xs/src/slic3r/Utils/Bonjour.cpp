#include "Bonjour.hpp"

#include <cstdint>
#include <algorithm>
#include <array>
#include <vector>
#include <string>
#include <random>
#include <thread>
#include <boost/optional.hpp>
#include <boost/system/error_code.hpp>
#include <boost/endian/conversion.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/format.hpp>

using boost::optional;
using boost::system::error_code;
namespace endian = boost::endian;
namespace asio = boost::asio;
using boost::asio::ip::udp;


namespace Slic3r {


// Minimal implementation of a MDNS/DNS-SD client
// This implementation is extremely simple, only the bits that are useful
// for basic MDNS discovery of OctoPi devices are present.
// However, the bits that are present are implemented with security in mind.
// Only fully correct DNS replies are allowed through.
// While decoding the decoder will bail the moment it encounters anything fishy.
// At least that's the idea. To help prove this is actually the case,
// the implementations has been tested with AFL.


struct DnsName: public std::string
{
	enum
	{
		MAX_RECURSION = 10,     // Keep this low
	};

	static optional<DnsName> decode(const std::vector<char> &buffer, size_t &offset, unsigned depth = 0)
	{
		// Check offset sanity:
		if (offset + 1 >= buffer.size()) {
			return boost::none;
		}

		// Check for recursion depth to prevent parsing names that are nested too deeply or end up cyclic:
		if (depth >= MAX_RECURSION) {
			return boost::none;
		}

		DnsName res;
		const size_t bsize = buffer.size();

		while (true) {
			const char* ptr = buffer.data() + offset;
			unsigned len = static_cast<unsigned char>(*ptr);
			if (len & 0xc0) {
				// This is a recursive label
				unsigned len_2 = static_cast<unsigned char>(ptr[1]);
				size_t pointer = (len & 0x3f) << 8 | len_2;
				const auto nested = decode(buffer, pointer, depth + 1);
				if (!nested) {
					return boost::none;
				} else {
					if (res.size() > 0) {
						res.push_back('.');
					}
					res.append(*nested);
					offset += 2;
					return std::move(res);
				}
			} else if (len == 0) {
				// This is a name terminator
				offset++;
				break;
			} else {
				// This is a regular label
				len &= 0x3f;
				if (len + offset + 1 >= bsize) {
					return boost::none;
				}

				res.reserve(len);
				if (res.size() > 0) {
					res.push_back('.');
				}

				ptr++;
				for (const auto end = ptr + len; ptr < end; ptr++) {
					char c = *ptr;
					if (c >= 0x20 && c <= 0x7f) {
						res.push_back(c);
					} else {
						return boost::none;
					}
				}

				offset += len + 1;
			}
		}

		if (res.size() > 0) {
			return std::move(res);
		} else {
			return boost::none;
		}
	}
};

struct DnsHeader
{
	uint16_t id;
	uint16_t flags;
	uint16_t qdcount;
	uint16_t ancount;
	uint16_t nscount;
	uint16_t arcount;

	enum
	{
		SIZE = 12,
	};

	static DnsHeader decode(const std::vector<char> &buffer) {
		DnsHeader res;
		const uint16_t *data_16 = reinterpret_cast<const uint16_t*>(buffer.data());
		res.id = endian::big_to_native(data_16[0]);
		res.flags = endian::big_to_native(data_16[1]);
		res.qdcount = endian::big_to_native(data_16[2]);
		res.ancount = endian::big_to_native(data_16[3]);
		res.nscount = endian::big_to_native(data_16[4]);
		res.arcount = endian::big_to_native(data_16[5]);
		return res;
	}

	uint32_t rrcount() const {
		return ancount + nscount + arcount;
	}
};

struct DnsQuestion
{
	enum
	{
		MIN_SIZE = 5,
	};

	DnsName name;
	uint16_t type;
	uint16_t qclass;

	DnsQuestion() :
		type(0),
		qclass(0)
	{}

	static optional<DnsQuestion> decode(const std::vector<char> &buffer, size_t &offset)
	{
		auto qname = DnsName::decode(buffer, offset);
		if (!qname) {
			return boost::none;
		}

		DnsQuestion res;
		res.name = std::move(*qname);
		const uint16_t *data_16 = reinterpret_cast<const uint16_t*>(buffer.data() + offset);
		res.type = endian::big_to_native(data_16[0]);
		res.qclass = endian::big_to_native(data_16[1]);

		offset += 4;
		return std::move(res);
	}
};

struct DnsResource
{
	DnsName name;
	uint16_t type;
	uint16_t rclass;
	uint32_t ttl;
	std::vector<char> data;

	DnsResource() :
		type(0),
		rclass(0),
		ttl(0)
	{}

	static optional<DnsResource> decode(const std::vector<char> &buffer, size_t &offset, size_t &dataoffset)
	{
		const size_t bsize = buffer.size();
		if (offset + 1 >= bsize) {
			return boost::none;
		}

		auto rname = DnsName::decode(buffer, offset);
		if (!rname) {
			return boost::none;
		}

		if (offset + 10 >= bsize) {
			return boost::none;
		}

		DnsResource res;
		res.name = std::move(*rname);
		const uint16_t *data_16 = reinterpret_cast<const uint16_t*>(buffer.data() + offset);
		res.type = endian::big_to_native(data_16[0]);
		res.rclass = endian::big_to_native(data_16[1]);
		res.ttl = endian::big_to_native(*reinterpret_cast<const uint32_t*>(data_16 + 2));
		uint16_t rdlength = endian::big_to_native(data_16[4]);

		offset += 10;
		if (offset + rdlength > bsize) {
			return boost::none;
		}

		dataoffset = offset;
		res.data = std::move(std::vector<char>(buffer.begin() + offset, buffer.begin() + offset + rdlength));
		offset += rdlength;

		return std::move(res);
	}
};

struct DnsRR_A
{
	enum { TAG = 0x1 };

	asio::ip::address_v4 ip;

	static void decode(optional<DnsRR_A> &result, const DnsResource &rr)
	{
		if (rr.data.size() == 4) {
			DnsRR_A res;
			const uint32_t ip = endian::big_to_native(*reinterpret_cast<const uint32_t*>(rr.data.data()));
			res.ip = asio::ip::address_v4(ip);
			result = std::move(res);
		}
	}
};

struct DnsRR_AAAA
{
	enum { TAG = 0x1c };

	asio::ip::address_v6 ip;

	static void decode(optional<DnsRR_AAAA> &result, const DnsResource &rr)
	{
		if (rr.data.size() == 16) {
			DnsRR_AAAA res;
			std::array<unsigned char, 16> ip;
			std::copy_n(rr.data.begin(), 16, ip.begin());
			res.ip = asio::ip::address_v6(ip);
			result = std::move(res);
		}
	}
};

struct DnsRR_SRV
{
	enum
	{
		TAG = 0x21,
		MIN_SIZE = 8,
	};

	uint16_t priority;
	uint16_t weight;
	uint16_t port;
	DnsName hostname;

	static optional<DnsRR_SRV> decode(const std::vector<char> &buffer, const DnsResource &rr, size_t dataoffset)
	{
		if (rr.data.size() < MIN_SIZE) {
			return boost::none;
		}

		DnsRR_SRV res;

		const uint16_t *data_16 = reinterpret_cast<const uint16_t*>(rr.data.data());
		res.priority = endian::big_to_native(data_16[0]);
		res.weight = endian::big_to_native(data_16[1]);
		res.port = endian::big_to_native(data_16[2]);

		size_t offset = dataoffset + 6;
		auto hostname = DnsName::decode(buffer, offset);

		if (hostname) {
			res.hostname = std::move(*hostname);
			return std::move(res);
		} else {
			return boost::none;
		}
	}
};

struct DnsRR_TXT
{
	enum
	{
		TAG = 0x10,
	};

	std::vector<std::string> values;

	static optional<DnsRR_TXT> decode(const DnsResource &rr)
	{
		const size_t size = rr.data.size();
		if (size < 2) {
			return boost::none;
		}

		DnsRR_TXT res;

		for (auto it = rr.data.begin(); it != rr.data.end(); ) {
			unsigned val_size = static_cast<unsigned char>(*it);
			if (val_size == 0 || it + val_size >= rr.data.end()) {
				return boost::none;
			}
			++it;

			std::string value(val_size, ' ');
			std::copy(it, it + val_size, value.begin());
			res.values.push_back(std::move(value));

			it += val_size;
		}

		return std::move(res);
	}
};

struct DnsSDPair
{
	optional<DnsRR_SRV> srv;
	optional<DnsRR_TXT> txt;
};

struct DnsSDMap : public std::map<std::string, DnsSDPair>
{
	void insert_srv(std::string &&name, DnsRR_SRV &&srv)
	{
		auto hit = this->find(name);
		if (hit != this->end()) {
			hit->second.srv = std::move(srv);
		} else {
			DnsSDPair pair;
			pair.srv = std::move(srv);
			this->insert(std::make_pair(std::move(name), std::move(pair)));
		}
	}

	void insert_txt(std::string &&name, DnsRR_TXT &&txt)
	{
		auto hit = this->find(name);
		if (hit != this->end()) {
			hit->second.txt = std::move(txt);
		} else {
			DnsSDPair pair;
			pair.txt = std::move(txt);
			this->insert(std::make_pair(std::move(name), std::move(pair)));
		}
	}
};

struct DnsMessage
{
	enum
	{
		MAX_SIZE = 4096,
		MAX_ANS = 30,
	};

	DnsHeader header;
	optional<DnsQuestion> question;

	optional<DnsRR_A> rr_a;
	optional<DnsRR_AAAA> rr_aaaa;
	std::vector<DnsRR_SRV> rr_srv;

	DnsSDMap sdmap;

	static optional<DnsMessage> decode(const std::vector<char> &buffer, optional<uint16_t> id_wanted = boost::none)
	{
		const auto size = buffer.size();
		if (size < DnsHeader::SIZE + DnsQuestion::MIN_SIZE || size > MAX_SIZE) {
			return boost::none;
		}

		DnsMessage res;
		res.header = DnsHeader::decode(buffer);

		if (id_wanted && *id_wanted != res.header.id) {
			return boost::none;
		}

		if (res.header.qdcount > 1 || res.header.ancount > MAX_ANS) {
			return boost::none;
		}

		size_t offset = DnsHeader::SIZE;
		if (res.header.qdcount == 1) {
			res.question = DnsQuestion::decode(buffer, offset);
		}

		for (unsigned i = 0; i < res.header.rrcount(); i++) {
			size_t dataoffset = 0;
			auto rr = DnsResource::decode(buffer, offset, dataoffset);
			if (!rr) {
				return boost::none;
			} else {
				res.parse_rr(buffer, std::move(*rr), dataoffset);
			}
		}

		return std::move(res);
	}
private:
	void parse_rr(const std::vector<char> &buffer, DnsResource &&rr, size_t dataoffset)
	{
		switch (rr.type) {
			case DnsRR_A::TAG: DnsRR_A::decode(this->rr_a, rr); break;
			case DnsRR_AAAA::TAG: DnsRR_AAAA::decode(this->rr_aaaa, rr); break;
			case DnsRR_SRV::TAG: {
				auto srv = DnsRR_SRV::decode(buffer, rr, dataoffset);
				if (srv) { this->sdmap.insert_srv(std::move(rr.name), std::move(*srv)); }
				break;
			}
			case DnsRR_TXT::TAG: {
				auto txt = DnsRR_TXT::decode(rr);
				if (txt) { this->sdmap.insert_txt(std::move(rr.name), std::move(*txt)); }
				break;
			}
		}
	}
};

std::ostream& operator<<(std::ostream &os, const DnsMessage &msg)
{
	os << "DnsMessage(ID: " << msg.header.id << ", "
		<< "Q: " << (msg.question ? msg.question->name.c_str() : "none") << ", "
		<< "A: " << (msg.rr_a ? msg.rr_a->ip.to_string() : "none") << ", "
		<< "AAAA: " << (msg.rr_aaaa ? msg.rr_aaaa->ip.to_string() : "none") << ", "
		<< "services: [";

		enum { SRV_PRINT_MAX = 3 };
		unsigned i = 0;
		for (const auto &sdpair : msg.sdmap) {
			os << sdpair.first << ", ";

			if (++i >= SRV_PRINT_MAX) {
				os << "...";
				break;
			}
		}

		os << "])";

	return os;
}


struct BonjourRequest
{
	static const asio::ip::address_v4 MCAST_IP4;
	static const uint16_t MCAST_PORT;

	uint16_t id;
	std::vector<char> data;

	static optional<BonjourRequest> make(const std::string &service, const std::string &protocol);

private:
	BonjourRequest(uint16_t id, std::vector<char> &&data) :
		id(id),
		data(std::move(data))
	{}
};

const asio::ip::address_v4 BonjourRequest::MCAST_IP4{0xe00000fb};
const uint16_t BonjourRequest::MCAST_PORT = 5353;

optional<BonjourRequest> BonjourRequest::make(const std::string &service, const std::string &protocol)
{
	if (service.size() > 15 || protocol.size() > 15) {
		return boost::none;
	}

	std::random_device dev;
	std::uniform_int_distribution<uint16_t> dist;
	uint16_t id = dist(dev);
	uint16_t id_big = endian::native_to_big(id);
	const char *id_char = reinterpret_cast<char*>(&id_big);

	std::vector<char> data;
	data.reserve(service.size() + 18);

	// Add the transaction ID
	data.push_back(id_char[0]);
	data.push_back(id_char[1]);

	// Add metadata
	static const unsigned char rq_meta[] = {
		0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};
	std::copy(rq_meta, rq_meta + sizeof(rq_meta), std::back_inserter(data));

	// Add PTR query name
	data.push_back(service.size() + 1);
	data.push_back('_');
	data.insert(data.end(), service.begin(), service.end());
	data.push_back(protocol.size() + 1);
	data.push_back('_');
	data.insert(data.end(), protocol.begin(), protocol.end());

	// Add the rest of PTR record
	static const unsigned char ptr_tail[] = {
		0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0xff,
	};
	std::copy(ptr_tail, ptr_tail + sizeof(ptr_tail), std::back_inserter(data));

	return BonjourRequest(id, std::move(data));
}


// API - private part

struct Bonjour::priv
{
	const std::string service;
	const std::string protocol;
	const std::string service_dn;
	unsigned timeout;
	unsigned retries;
	uint16_t rq_id;

	std::vector<char> buffer;
	std::thread io_thread;
	Bonjour::ReplyFn replyfn;
	Bonjour::CompleteFn completefn;

	priv(std::string service, std::string protocol);

	std::string strip_service_dn(const std::string &service_name) const;
	void udp_receive(udp::endpoint from, size_t bytes);
	void lookup_perform();
};

Bonjour::priv::priv(std::string service, std::string protocol) :
	service(std::move(service)),
	protocol(std::move(protocol)),
	service_dn((boost::format("_%1%._%2%.local") % this->service % this->protocol).str()),
	timeout(10),
	retries(1),
	rq_id(0)
{
	buffer.resize(DnsMessage::MAX_SIZE);
}

std::string Bonjour::priv::strip_service_dn(const std::string &service_name) const
{
	if (service_name.size() <= service_dn.size()) {
		return service_name;
	}

	auto needle = service_name.rfind(service_dn);
	if (needle == service_name.size() - service_dn.size()) {
		return service_name.substr(0, needle - 1);
	} else {
		return service_name;
	}
}

void Bonjour::priv::udp_receive(udp::endpoint from, size_t bytes)
{
	if (bytes == 0 || !replyfn) {
		return;
	}

	buffer.resize(bytes);
	const auto dns_msg = DnsMessage::decode(buffer, rq_id);
	if (dns_msg) {
		asio::ip::address ip = from.address();
		if (dns_msg->rr_a) { ip = dns_msg->rr_a->ip; }
		else if (dns_msg->rr_aaaa) { ip = dns_msg->rr_aaaa->ip; }

		for (const auto &sdpair : dns_msg->sdmap) {
			if (! sdpair.second.srv) {
				continue;
			}

			const auto &srv = *sdpair.second.srv;
			auto service_name = strip_service_dn(sdpair.first);

			std::string path;
			std::string version;

			if (sdpair.second.txt) {
				static const std::string tag_path = "path=";
				static const std::string tag_version = "version=";

				for (const auto &value : sdpair.second.txt->values) {
					if (value.size() > tag_path.size() && value.compare(0, tag_path.size(), tag_path) == 0) {
						path = std::move(value.substr(tag_path.size()));
					} else if (value.size() > tag_version.size() && value.compare(0, tag_version.size(), tag_version) == 0) {
						version = std::move(value.substr(tag_version.size()));
					}
				}
			}

			BonjourReply reply(ip, srv.port, std::move(service_name), srv.hostname, std::move(path), std::move(version));
			replyfn(std::move(reply));
		}
	}
}

void Bonjour::priv::lookup_perform()
{
	const auto brq = BonjourRequest::make(service, protocol);
	if (!brq) {
		return;
	}

	auto self = this;
	rq_id = brq->id;

	try {
		boost::asio::io_service io_service;
		udp::socket socket(io_service);
		socket.open(udp::v4());
		socket.set_option(udp::socket::reuse_address(true));
		udp::endpoint mcast(BonjourRequest::MCAST_IP4, BonjourRequest::MCAST_PORT);
		socket.send_to(asio::buffer(brq->data), mcast);

		bool expired = false;
		bool retry = false;
		asio::deadline_timer timer(io_service);
		retries--;
		std::function<void(const error_code &)> timer_handler = [&](const error_code &error) {
			if (retries == 0 || error) {
				expired = true;
				if (self->completefn) {
					self->completefn();
				}
			} else {
				retry = true;
				retries--;
				timer.expires_from_now(boost::posix_time::seconds(timeout));
				timer.async_wait(timer_handler);
			}
		};

		timer.expires_from_now(boost::posix_time::seconds(timeout));
		timer.async_wait(timer_handler);

		udp::endpoint recv_from;
		const auto recv_handler = [&](const error_code &error, size_t bytes) {
			if (!error) { self->udp_receive(recv_from, bytes); }
		};
		socket.async_receive_from(asio::buffer(buffer, buffer.size()), recv_from, recv_handler);

		while (io_service.run_one()) {
			if (expired) {
				socket.cancel();
			} else if (retry) {
				retry = false;
				socket.send_to(asio::buffer(brq->data), mcast);
			} else {
				buffer.resize(DnsMessage::MAX_SIZE);
				socket.async_receive_from(asio::buffer(buffer, buffer.size()), recv_from, recv_handler);
			}
		}
	} catch (std::exception& e) {
	}
}


// API - public part

BonjourReply::BonjourReply(boost::asio::ip::address ip, uint16_t port, std::string service_name, std::string hostname, std::string path, std::string version) :
	ip(std::move(ip)),
	port(port),
	service_name(std::move(service_name)),
	hostname(std::move(hostname)),
	path(path.empty() ? std::move(std::string("/")) : std::move(path)),
	version(version.empty() ? std::move(std::string("Unknown")) : std::move(version))
{
	std::string proto;
	std::string port_suffix;
	if (port == 443) { proto = "https://"; }
	if (port != 443 && port != 80) { port_suffix = std::to_string(port).insert(0, 1, ':'); }
	if (this->path[0] != '/') { this->path.insert(0, 1, '/'); }
	full_address = proto + ip.to_string() + port_suffix;
	if (this->path != "/") { full_address += path; }
}

bool BonjourReply::operator==(const BonjourReply &other) const
{
	return this->full_address == other.full_address
		&& this->service_name == other.service_name;
}

bool BonjourReply::operator<(const BonjourReply &other) const
{
	if (this->ip != other.ip) {
		// So that the common case doesn't involve string comparison
		return this->ip < other.ip;
	} else {
		auto cmp = this->full_address.compare(other.full_address);
		return cmp != 0 ? cmp < 0 : this->service_name < other.service_name;
	}
}

std::ostream& operator<<(std::ostream &os, const BonjourReply &reply)
{
	os << "BonjourReply(" << reply.ip.to_string() << ", " << reply.service_name << ", "
		<< reply.hostname << ", " << reply.path << ", " << reply.version << ")";
	return os;
}


Bonjour::Bonjour(std::string service, std::string protocol) :
	p(new priv(std::move(service), std::move(protocol)))
{}

Bonjour::Bonjour(Bonjour &&other) : p(std::move(other.p)) {}

Bonjour::~Bonjour()
{
	if (p && p->io_thread.joinable()) {
		p->io_thread.detach();
	}
}

Bonjour& Bonjour::set_timeout(unsigned timeout)
{
	if (p) { p->timeout = timeout; }
	return *this;
}

Bonjour& Bonjour::set_retries(unsigned retries)
{
	if (p && retries > 0) { p->retries = retries; }
	return *this;
}

Bonjour& Bonjour::on_reply(ReplyFn fn)
{
	if (p) { p->replyfn = std::move(fn); }
	return *this;
}

Bonjour& Bonjour::on_complete(CompleteFn fn)
{
	if (p) { p->completefn = std::move(fn); }
	return *this;
}

Bonjour::Ptr Bonjour::lookup()
{
	auto self = std::make_shared<Bonjour>(std::move(*this));

	if (self->p) {
		auto io_thread = std::thread([self]() {
				self->p->lookup_perform();
			});
		self->p->io_thread = std::move(io_thread);
	}

	return self;
}


}

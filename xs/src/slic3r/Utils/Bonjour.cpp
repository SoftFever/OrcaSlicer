#include "Bonjour.hpp"

#include <cstdint>
#include <algorithm>
#include <unordered_map>
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


// TODO: Fuzzing test

namespace Slic3r {


// Miniman implementation of a MDNS client
// This implementation is extremely simple, only the bits that are useful
// for very basic MDNS discovery are present.

struct DnsName: public std::string
{
	enum
	{
		MAX_RECURSION = 10,     // Keep this low
	};

	static optional<DnsName> decode(const std::vector<char> &buffer, ptrdiff_t &offset, unsigned depth = 0)
	{
		// We trust that the offset passed is bounds-checked properly,
		// including that there is at least one byte beyond that offset.
		// Any further arithmetic has to be bounds-checked here though.

		// Check for recursion depth to prevent parsing names that are nested too deeply
		// or end up cyclic:
		if (depth >= MAX_RECURSION) {
			return boost::none;
		}

		DnsName res;
		const ptrdiff_t bsize = buffer.size();

		while (true) {
			const char* ptr = buffer.data() + offset;
			char len = *ptr;
			if (len & 0xc0) {
				// This is a recursive label
				ptrdiff_t pointer = (len & 0x3f) << 8 | ptr[1];
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

	static optional<DnsQuestion> decode(const std::vector<char> &buffer, ptrdiff_t &offset)
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

	static optional<DnsResource> decode(const std::vector<char> &buffer, ptrdiff_t &offset, ptrdiff_t &dataoffset)
	{
		auto rname = DnsName::decode(buffer, offset);
		if (!rname) {
			return boost::none;
		}

		const ptrdiff_t bsize = buffer.size();

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
	std::string name;
	std::string service;
	DnsName hostname;

	static void decode(std::vector<DnsRR_SRV> &results, const std::vector<char> &buffer, const DnsResource &rr, ptrdiff_t dataoffset)
	{
		if (rr.data.size() < MIN_SIZE) {
			return;
		}

		DnsRR_SRV res;

		{
			const auto dot_pos = rr.name.find_first_of('.');
			if (dot_pos > 0 && dot_pos < rr.name.size() - 1) {
				res.name = rr.name.substr(0, dot_pos);
				res.service = rr.name.substr(dot_pos + 1);
			} else {
				return;
			}
		}

		const uint16_t *data_16 = reinterpret_cast<const uint16_t*>(rr.data.data());
		res.priority = endian::big_to_native(data_16[0]);
		res.weight = endian::big_to_native(data_16[1]);
		res.port = endian::big_to_native(data_16[2]);

		ptrdiff_t offset = dataoffset + 6;
		auto hostname = DnsName::decode(buffer, offset);

		if (hostname) {
			res.hostname = std::move(*hostname);
			results.emplace_back(std::move(res));
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
	std::vector<DnsResource> answers;

	optional<DnsRR_A> rr_a;
	optional<DnsRR_AAAA> rr_aaaa;
	std::vector<DnsRR_SRV> rr_srv;

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

		ptrdiff_t offset = DnsHeader::SIZE;
		if (res.header.qdcount == 1) {
			res.question = DnsQuestion::decode(buffer, offset);
		}

		for (unsigned i = 0; i < res.header.rrcount(); i++) {
			ptrdiff_t dataoffset = 0;
			auto rr = DnsResource::decode(buffer, offset, dataoffset);
			if (!rr) {
				return boost::none;
			} else {
				res.parse_rr(buffer, *rr, dataoffset);
				res.answers.push_back(std::move(*rr));
			}
		}

		return std::move(res);
	}
private:
	void parse_rr(const std::vector<char> &buffer, const DnsResource &rr, ptrdiff_t dataoffset)
	{
		switch (rr.type) {
			case DnsRR_A::TAG: DnsRR_A::decode(this->rr_a, rr); break;
			case DnsRR_AAAA::TAG: DnsRR_AAAA::decode(this->rr_aaaa, rr); break;
			case DnsRR_SRV::TAG: DnsRR_SRV::decode(this->rr_srv, buffer, rr, dataoffset); break;
		}
	}
};


struct BonjourRequest
{
	static const asio::ip::address_v4 MCAST_IP4;
	static const uint16_t MCAST_PORT;

	static const char rq_template[];

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

const char BonjourRequest::rq_template[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x5f, 0x68, 0x74,
	0x74, 0x70, 0x04, 0x5f, 0x74, 0x63, 0x70, 0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c,
	0x00, 0x01,
};

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
	static const char rq_meta[] = {
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
	static const char ptr_tail[] = {
		0x05, 0x6c, 0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0x01,
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
	uint16_t rq_id;

	std::vector<char> buffer;
	std::thread io_thread;
	Bonjour::ReplyFn replyfn;
	Bonjour::CompleteFn completefn;

	priv(std::string service, std::string protocol);

	void udp_receive(const error_code &error, size_t bytes);
	void lookup_perform();
};

Bonjour::priv::priv(std::string service, std::string protocol) :
	service(std::move(service)),
	protocol(std::move(protocol)),
	service_dn((boost::format("_%1%._%2%.local") % this->service % this->protocol).str()),
	timeout(10),
	rq_id(0)
{
	buffer.resize(DnsMessage::MAX_SIZE);
}

void Bonjour::priv::udp_receive(const error_code &error, size_t bytes)
{
	if (error || bytes == 0 || !replyfn) {
		return;
	}

	buffer.resize(bytes);
	const auto dns_msg = DnsMessage::decode(buffer, rq_id);
	if (dns_msg) {
		std::string ip;
		if (dns_msg->rr_a) { ip = dns_msg->rr_a->ip.to_string(); }
		else if (dns_msg->rr_aaaa) { ip = dns_msg->rr_aaaa->ip.to_string(); }

		for (const auto &srv : dns_msg->rr_srv) {
			if (srv.service == service_dn) {
				replyfn(std::move(ip), std::move(srv.hostname), std::move(srv.name));
			}
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

		bool timeout = false;
		asio::deadline_timer timer(io_service);
		timer.expires_from_now(boost::posix_time::seconds(10));
		timer.async_wait([=, &timeout](const error_code &error) {
			timeout = true;
			if (self->completefn) {
				self->completefn();
			}
		});

		const auto recv_handler = [=](const error_code &error, size_t bytes) {
			self->udp_receive(error, bytes);
		};
		socket.async_receive(asio::buffer(buffer, buffer.size()), recv_handler);

		while (io_service.run_one()) {
			if (timeout) {
				socket.cancel();
			} else {
				buffer.resize(DnsMessage::MAX_SIZE);
				socket.async_receive(asio::buffer(buffer, buffer.size()), recv_handler);
			}
		}
	} catch (std::exception& e) {
	}
}


// API - public part

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
		auto io_thread = std::thread([self](){
				self->p->lookup_perform();
			});
		self->p->io_thread = std::move(io_thread);
	}

	return self;
}


void Bonjour::pokus()   // XXX
{
	// auto bonjour = Bonjour("http")
	//     .set_timeout(15)
	//     .on_reply([](std::string ip, std::string host, std::string service_name) {
	//         std::cerr << "MDNS: " << ip << " = " << host << " : " << service_name << std::endl;
	//     })
	//     .on_complete([](){
	//         std::cerr << "MDNS lookup complete" << std::endl;
	//     })
	//     .lookup();
}


}

#include "Http.hpp"

#include <cstdlib>
#include <functional>
#include <thread>
#include <tuple>
#include <boost/format.hpp>

#include <curl/curl.h>

#include "../../libslic3r/libslic3r.h"


namespace Slic3r {


// Private

class CurlGlobalInit
{
	static const CurlGlobalInit instance;

	CurlGlobalInit()  { ::curl_global_init(CURL_GLOBAL_DEFAULT); }
	~CurlGlobalInit() { ::curl_global_cleanup(); }
};

struct Http::priv
{
	enum {
		DEFAULT_SIZE_LIMIT = 5 * 1024 * 1024,
	};

	::CURL *curl;
	::curl_httppost *form;
	::curl_httppost *form_end;
	::curl_slist *headerlist;
	std::string buffer;
	size_t limit;
	bool cancel;

	std::thread io_thread;
	Http::CompleteFn completefn;
	Http::ErrorFn errorfn;
	Http::ProgressFn progressfn;

	priv(const std::string &url);
	~priv();

	static bool ca_file_supported(::CURL *curl);
	static size_t writecb(void *data, size_t size, size_t nmemb, void *userp);
	static int xfercb(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow);
	static int xfercb_legacy(void *userp, double dltotal, double dlnow, double ultotal, double ulnow);
	std::string curl_error(CURLcode curlcode);
	std::string body_size_error();
	void http_perform();
};

Http::priv::priv(const std::string &url) :
	curl(::curl_easy_init()),
	form(nullptr),
	form_end(nullptr),
	headerlist(nullptr),
	cancel(false)
{
	if (curl == nullptr) {
		throw std::runtime_error(std::string("Could not construct Curl object"));
	}

	::curl_easy_setopt(curl, CURLOPT_URL, url.c_str());   // curl makes a copy internally
	::curl_easy_setopt(curl, CURLOPT_USERAGENT, SLIC3R_FORK_NAME "/" SLIC3R_VERSION);
}

Http::priv::~priv()
{
	::curl_easy_cleanup(curl);
	::curl_formfree(form);
	::curl_slist_free_all(headerlist);
}

bool Http::priv::ca_file_supported(::CURL *curl)
{
#ifdef _WIN32
	bool res = false;
#else
	bool res = true;
#endif

	if (curl == nullptr) { return res; }

#if LIBCURL_VERSION_MAJOR >= 7 && LIBCURL_VERSION_MINOR >= 48
	::curl_tlssessioninfo *tls;
	if (::curl_easy_getinfo(curl, CURLINFO_TLS_SSL_PTR, &tls) == CURLE_OK) {
		if (tls->backend == CURLSSLBACKEND_SCHANNEL || tls->backend == CURLSSLBACKEND_DARWINSSL) {
			// With Windows and OS X native SSL support, cert files cannot be set
			res = false;
		}
	}
#endif

	return res;
}

size_t Http::priv::writecb(void *data, size_t size, size_t nmemb, void *userp)
{
	auto self = static_cast<priv*>(userp);
	const char *cdata = static_cast<char*>(data);
	const size_t realsize = size * nmemb;

	const size_t limit = self->limit > 0 ? self->limit : DEFAULT_SIZE_LIMIT;
	if (self->buffer.size() + realsize > limit) {
		// This makes curl_easy_perform return CURLE_WRITE_ERROR
		return 0;
	}

	self->buffer.append(cdata, realsize);

	return realsize;
}

int Http::priv::xfercb(void *userp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	auto self = static_cast<priv*>(userp);
	bool cb_cancel = false;

	if (self->progressfn) {
		Progress progress(dltotal, dlnow, ultotal, ulnow);
		self->progressfn(progress, cb_cancel);
	}

	return self->cancel || cb_cancel;
}

int Http::priv::xfercb_legacy(void *userp, double dltotal, double dlnow, double ultotal, double ulnow)
{
	return xfercb(userp, dltotal, dlnow, ultotal, ulnow);
}

std::string Http::priv::curl_error(CURLcode curlcode)
{
	return (boost::format("%1% (%2%)")
		% ::curl_easy_strerror(curlcode)
		% curlcode
	).str();
}

std::string Http::priv::body_size_error()
{
	return (boost::format("HTTP body data size exceeded limit (%1% bytes)") % limit).str();
}

void Http::priv::http_perform()
{
	::curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	::curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	::curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writecb);
	::curl_easy_setopt(curl, CURLOPT_WRITEDATA, static_cast<void*>(this));

	::curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
#if LIBCURL_VERSION_MAJOR >= 7 && LIBCURL_VERSION_MINOR >= 32
	::curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xfercb);
	::curl_easy_setopt(curl, CURLOPT_XFERINFODATA, static_cast<void*>(this));
	(void)xfercb_legacy;   // prevent unused function warning
#else
	::curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, xfercb);
	::curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, static_cast<void*>(this));
#endif

#ifndef NDEBUG
	::curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
#endif

	if (headerlist != nullptr) {
		::curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerlist);
	}

	if (form != nullptr) {
		::curl_easy_setopt(curl, CURLOPT_HTTPPOST, form);
	}

	CURLcode res = ::curl_easy_perform(curl);
	long http_status = 0;
	::curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);

	if (res != CURLE_OK) {
		if (res == CURLE_ABORTED_BY_CALLBACK) {
			Progress dummyprogress(0, 0, 0, 0);
			bool cancel = true;
			if (progressfn) { progressfn(dummyprogress, cancel); }
		}
		else if (res == CURLE_WRITE_ERROR) {
			if (errorfn) { errorfn(std::move(buffer), std::move(body_size_error()), http_status); }
		} else {
			if (errorfn) { errorfn(std::move(buffer), std::move(curl_error(res)), http_status); }
		};
	} else {
		if (completefn) {
			completefn(std::move(buffer), http_status);
		}
	}
}

Http::Http(const std::string &url) : p(new priv(url)) {}


// Public

Http::Http(Http &&other) : p(std::move(other.p)) {}

Http::~Http()
{
	if (p && p->io_thread.joinable()) {
		p->io_thread.detach();
	}
}


Http& Http::size_limit(size_t sizeLimit)
{
	if (p) { p->limit = sizeLimit; }
	return *this;
}

Http& Http::header(std::string name, const std::string &value)
{
	if (!p) { return * this; }

	if (name.size() > 0) {
		name.append(": ").append(value);
	} else {
		name.push_back(':');
	}
	p->headerlist = curl_slist_append(p->headerlist, name.c_str());
	return *this;
}

Http& Http::remove_header(std::string name)
{
	if (p) {
		name.push_back(':');
		p->headerlist = curl_slist_append(p->headerlist, name.c_str());
	}

	return *this;
}

Http& Http::ca_file(const std::string &name)
{
	if (p && priv::ca_file_supported(p->curl)) {
		::curl_easy_setopt(p->curl, CURLOPT_CAINFO, name.c_str());
	}

	return *this;
}

Http& Http::form_add(const std::string &name, const std::string &contents)
{
	if (p) {
		::curl_formadd(&p->form, &p->form_end,
			CURLFORM_COPYNAME, name.c_str(),
			CURLFORM_COPYCONTENTS, contents.c_str(),
			CURLFORM_END
		);
	}

	return *this;
}

Http& Http::form_add_file(const std::string &name, const std::string &filename)
{
	if (p) {
		::curl_formadd(&p->form, &p->form_end,
			CURLFORM_COPYNAME, name.c_str(),
			CURLFORM_FILE, filename.c_str(),
			CURLFORM_CONTENTTYPE, "application/octet-stream",
			CURLFORM_END
		);
	}

	return *this;
}

Http& Http::on_complete(CompleteFn fn)
{
	if (p) { p->completefn = std::move(fn); }
	return *this;
}

Http& Http::on_error(ErrorFn fn)
{
	if (p) { p->errorfn = std::move(fn); }
	return *this;
}

Http& Http::on_progress(ProgressFn fn)
{
	if (p) { p->progressfn = std::move(fn); }
	return *this;
}

Http::Ptr Http::perform()
{
	auto self = std::make_shared<Http>(std::move(*this));

	if (self->p) {
		auto io_thread = std::thread([self](){
				self->p->http_perform();
			});
		self->p->io_thread = std::move(io_thread);
	}

	return self;
}

void Http::perform_sync()
{
	if (p) { p->http_perform(); }
}

void Http::cancel()
{
	if (p) { p->cancel = true; }
}

Http Http::get(std::string url)
{
	return std::move(Http{std::move(url)});
}

Http Http::post(std::string url)
{
	Http http{std::move(url)};
	curl_easy_setopt(http.p->curl, CURLOPT_POST, 1L);
	return http;
}

bool Http::ca_file_supported()
{
	::CURL *curl = ::curl_easy_init();
	bool res = priv::ca_file_supported(curl);
	if (curl != nullptr) { ::curl_easy_cleanup(curl); }
	return res;
}

std::ostream& operator<<(std::ostream &os, const Http::Progress &progress)
{
	os << "Http::Progress("
		<< "dltotal = " << progress.dltotal
		<< ", dlnow = " << progress.dlnow
		<< ", ultotal = " << progress.ultotal
		<< ", ulnow = " << progress.ulnow
		<< ")";
	return os;
}


}

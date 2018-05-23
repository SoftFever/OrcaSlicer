#ifndef slic3r_Http_hpp_
#define slic3r_Http_hpp_

#include <memory>
#include <string>
#include <functional>


namespace Slic3r {


/// Represetns a Http request
class Http : public std::enable_shared_from_this<Http> {
private:
	struct priv;
public:
	struct Progress
	{
		size_t dltotal;   // Total bytes to download
		size_t dlnow;     // Bytes downloaded so far
		size_t ultotal;   // Total bytes to upload
		size_t ulnow;     // Bytes uploaded so far

		Progress(size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow) :
			dltotal(dltotal), dlnow(dlnow), ultotal(ultotal), ulnow(ulnow)
		{}
	};

	typedef std::shared_ptr<Http> Ptr;
	typedef std::function<void(std::string /* body */, unsigned /* http_status */)> CompleteFn;
	typedef std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> ErrorFn;
	typedef std::function<void(Progress, bool& /* cancel */)> ProgressFn;

	Http(Http &&other);

	// These are the primary constructors that create a HTTP object
	// for a GET and a POST request respectively.
	static Http get(std::string url);
	static Http post(std::string url);
	~Http();

	Http(const Http &) = delete;
	Http& operator=(const Http &) = delete;
	Http& operator=(Http &&) = delete;

	// Sets a maximum size of the data that can be received. The default is 5MB.
	Http& size_limit(size_t sizeLimit);
	// Sets a HTTP header field.
	Http& header(std::string name, const std::string &value);
	// Removes a header field.
	Http& remove_header(std::string name);
	// Sets a CA certificate file for usage with HTTPS. This is only supported on some backends,
	// specifically, this is supported with OpenSSL and NOT supported with Windows and OS X native certificate store.
	// See also ca_file_supported().
	Http& ca_file(const std::string &filename);
	// Add a HTTP multipart form field
	Http& form_add(const std::string &name, const std::string &contents);
	// Add a HTTP multipart form file data contents
	Http& form_add_file(const std::string &name, const std::string &path);
	// Same as above except also override the file's filename with a custom one
	Http& form_add_file(const std::string &name, const std::string &path, const std::string &filename);

	// Callback called on HTTP request complete
	Http& on_complete(CompleteFn fn);
	// Callback called on an error occuring at any stage of the requests: Url parsing, DNS lookup,
	// TCP connection, HTTP transfer, and finally also when the response indicates an error (status >= 400).
	// Therefore, a response body may or may not be present.
	Http& on_error(ErrorFn fn);
	// Callback called on data download/upload prorgess (called fairly frequently).
	// See the `Progress` structure for description of the data passed.
	// Writing a true-ish value into the cancel reference parameter cancels the request.
	Http& on_progress(ProgressFn fn);

	// Starts performing the request in a background thread
	Ptr perform();
	// Starts performing the request on the current thread
	void perform_sync();
	// Cancels a request in progress
	void cancel();

	// Tells whether current backend supports seting up a CA file using ca_file()
	static bool ca_file_supported();
private:
	Http(const std::string &url);

	std::unique_ptr<priv> p;
};

std::ostream& operator<<(std::ostream &, const Http::Progress &);


}

#endif

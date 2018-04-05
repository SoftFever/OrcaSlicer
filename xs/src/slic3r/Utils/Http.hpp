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
		size_t dltotal;
		size_t dlnow;
		size_t ultotal;
		size_t ulnow;
		
		Progress(size_t dltotal, size_t dlnow, size_t ultotal, size_t ulnow) :
			dltotal(dltotal), dlnow(dlnow), ultotal(ultotal), ulnow(ulnow)
		{}
	};

	typedef std::shared_ptr<Http> Ptr;
	typedef std::function<void(std::string /* body */, unsigned /* http_status */)> CompleteFn;
	typedef std::function<void(std::string /* body */, std::string /* error */, unsigned /* http_status */)> ErrorFn;
	typedef std::function<void(Progress, bool& /* cancel */)> ProgressFn;

	Http(Http &&other);

	static Http get(std::string url);
	static Http post(std::string url);
	~Http();

	Http(const Http &) = delete;
	Http& operator=(const Http &) = delete;
	Http& operator=(Http &&) = delete;

	Http& size_limit(size_t sizeLimit);
	Http& header(std::string name, const std::string &value);
	Http& remove_header(std::string name);
	Http& ca_file(const std::string &filename);
	Http& form_add(const std::string &name, const std::string &contents);
	Http& form_add_file(const std::string &name, const std::string &filename);

	Http& on_complete(CompleteFn fn);
	Http& on_error(ErrorFn fn);
	Http& on_progress(ProgressFn fn);

	Ptr perform();
	void perform_sync();
	void cancel();

	static bool ca_file_supported();
private:
	Http(const std::string &url);

	std::unique_ptr<priv> p;
};

std::ostream& operator<<(std::ostream &, const Http::Progress &);


}

#endif

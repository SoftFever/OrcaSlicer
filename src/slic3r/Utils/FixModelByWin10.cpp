#ifdef HAS_WIN10SDK

#ifndef NOMINMAX
# define NOMINMAX
#endif

#include "FixModelByWin10.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <condition_variable>
#include <exception>
#include <string>
#include <thread>

#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>

#include <roapi.h>
// for ComPtr
#include <wrl/client.h>
// from C:/Program Files (x86)/Windows Kits/10/Include/10.0.17134.0/
#include <winrt/robuffer.h>
#include <winrt/windows.storage.provider.h>
#include <winrt/windows.graphics.printing3d.h>

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "../GUI/GUI.hpp"
#include "../GUI/PresetBundle.hpp"

#include <wx/msgdlg.h>
#include <wx/progdlg.h>

extern "C"{
	// from rapi.h
	typedef HRESULT (__stdcall* FunctionRoInitialize)(int);
	typedef HRESULT (__stdcall* FunctionRoUninitialize)();
	typedef HRESULT	(__stdcall* FunctionRoActivateInstance)(HSTRING activatableClassId, IInspectable **instance);
	typedef HRESULT (__stdcall* FunctionRoGetActivationFactory)(HSTRING activatableClassId, REFIID iid, void **factory);
	// from winstring.h
	typedef HRESULT	(__stdcall* FunctionWindowsCreateString)(LPCWSTR sourceString, UINT32  length, HSTRING *string);
	typedef HRESULT	(__stdcall* FunctionWindowsDelteString)(HSTRING string);
}

namespace Slic3r {

HMODULE							s_hRuntimeObjectLibrary  = nullptr;
FunctionRoInitialize			s_RoInitialize			 = nullptr;
FunctionRoUninitialize			s_RoUninitialize		 = nullptr;
FunctionRoActivateInstance		s_RoActivateInstance     = nullptr;
FunctionRoGetActivationFactory	s_RoGetActivationFactory = nullptr;
FunctionWindowsCreateString		s_WindowsCreateString    = nullptr;
FunctionWindowsDelteString		s_WindowsDeleteString    = nullptr;

bool winrt_load_runtime_object_library()
{
	if (s_hRuntimeObjectLibrary == nullptr)
		s_hRuntimeObjectLibrary = LoadLibrary(L"ComBase.dll");
	if (s_hRuntimeObjectLibrary != nullptr) {
		s_RoInitialize			 = (FunctionRoInitialize)			GetProcAddress(s_hRuntimeObjectLibrary, "RoInitialize");
		s_RoUninitialize		 = (FunctionRoUninitialize)			GetProcAddress(s_hRuntimeObjectLibrary, "RoUninitialize");
		s_RoActivateInstance	 = (FunctionRoActivateInstance)		GetProcAddress(s_hRuntimeObjectLibrary, "RoActivateInstance");
		s_RoGetActivationFactory = (FunctionRoGetActivationFactory)	GetProcAddress(s_hRuntimeObjectLibrary, "RoGetActivationFactory");
		s_WindowsCreateString	 = (FunctionWindowsCreateString)	GetProcAddress(s_hRuntimeObjectLibrary, "WindowsCreateString");
		s_WindowsDeleteString	 = (FunctionWindowsDelteString)		GetProcAddress(s_hRuntimeObjectLibrary, "WindowsDeleteString");
	}
	return s_RoInitialize && s_RoUninitialize && s_RoActivateInstance && s_WindowsCreateString && s_WindowsDeleteString;
}

static HRESULT winrt_activate_instance(const std::wstring &class_name, IInspectable **pinst)
{
	HSTRING hClassName;
	HRESULT hr = (*s_WindowsCreateString)(class_name.c_str(), class_name.size(), &hClassName);
	if (S_OK != hr) 
		return hr;
	hr = (*s_RoActivateInstance)(hClassName, pinst);
	(*s_WindowsDeleteString)(hClassName);
	return hr;
}

template<typename TYPE>
static HRESULT winrt_activate_instance(const std::wstring &class_name, TYPE **pinst)
{
	IInspectable *pinspectable = nullptr;
	HRESULT hr = winrt_activate_instance(class_name, &pinspectable);
	if (S_OK != hr)
		return hr;
	hr = pinspectable->QueryInterface(__uuidof(TYPE), (void**)pinst);
	pinspectable->Release();
	return hr;
}

static HRESULT winrt_get_activation_factory(const std::wstring &class_name, REFIID iid, void **pinst)
{
	HSTRING hClassName;
	HRESULT hr = (*s_WindowsCreateString)(class_name.c_str(), class_name.size(), &hClassName);
	if (S_OK != hr)
		return hr;
	hr = (*s_RoGetActivationFactory)(hClassName, iid, pinst);
	(*s_WindowsDeleteString)(hClassName);
	return hr;
}

template<typename TYPE>
static HRESULT winrt_get_activation_factory(const std::wstring &class_name, TYPE **pinst)
{
	return winrt_get_activation_factory(class_name, __uuidof(TYPE), reinterpret_cast<void**>(pinst));
}

// To be called often to test whether to cancel the operation.
typedef std::function<void ()> ThrowOnCancelFn;

template<typename T>
static AsyncStatus winrt_async_await(const Microsoft::WRL::ComPtr<T> &asyncAction, ThrowOnCancelFn throw_on_cancel, int blocking_tick_ms = 100)
{
	Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncInfo> asyncInfo;
	asyncAction.As(&asyncInfo);
	AsyncStatus status;
	// Ugly blocking loop until the RepairAsync call finishes.
//FIXME replace with a callback.
// https://social.msdn.microsoft.com/Forums/en-US/a5038fb4-b7b7-4504-969d-c102faa389fb/trying-to-block-an-async-operation-and-wait-for-a-particular-time?forum=vclanguage
	for (;;) {
		asyncInfo->get_Status(&status);
		if (status != AsyncStatus::Started)
			return status;
		throw_on_cancel();
		::Sleep(blocking_tick_ms);
	}
}

static HRESULT winrt_open_file_stream(
	const std::wstring									 &path,
	ABI::Windows::Storage::FileAccessMode				  mode,
	ABI::Windows::Storage::Streams::IRandomAccessStream **fileStream,
	ThrowOnCancelFn										  throw_on_cancel)
{
	// Get the file factory.
	Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFileStatics> fileFactory;
	HRESULT hr = winrt_get_activation_factory(L"Windows.Storage.StorageFile", fileFactory.GetAddressOf());
	if (FAILED(hr)) return hr;

	// Open the file asynchronously.
	HSTRING hstr_path;
	hr = (*s_WindowsCreateString)(path.c_str(), path.size(), &hstr_path);
	if (FAILED(hr)) return hr;
	Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::StorageFile*>> fileOpenAsync;
	hr = fileFactory->GetFileFromPathAsync(hstr_path, fileOpenAsync.GetAddressOf());
	if (FAILED(hr)) return hr;
	(*s_WindowsDeleteString)(hstr_path);

	// Wait until the file gets open, get the actual file.
	AsyncStatus status = winrt_async_await(fileOpenAsync, throw_on_cancel);
	Microsoft::WRL::ComPtr<ABI::Windows::Storage::IStorageFile> storageFile;
	if (status == AsyncStatus::Completed) {
		hr = fileOpenAsync->GetResults(storageFile.GetAddressOf());
	} else {
		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncInfo> asyncInfo;
		hr = fileOpenAsync.As(&asyncInfo);
		if (FAILED(hr)) return hr;
		HRESULT err;
		hr = asyncInfo->get_ErrorCode(&err);
		return FAILED(hr) ? hr : err;
	}

	Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::Streams::IRandomAccessStream*>> fileStreamAsync;
	hr = storageFile->OpenAsync(mode, fileStreamAsync.GetAddressOf());
	if (FAILED(hr)) return hr;

	status = winrt_async_await(fileStreamAsync, throw_on_cancel);
	if (status == AsyncStatus::Completed) {
		hr = fileStreamAsync->GetResults(fileStream);
	} else {
		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncInfo> asyncInfo;
		hr = fileStreamAsync.As(&asyncInfo);
		if (FAILED(hr)) return hr;
		HRESULT err;
		hr = asyncInfo->get_ErrorCode(&err);
		if (!FAILED(hr))
			hr = err;
	}
	return hr;
}

bool is_windows10()
{
	HKEY hKey;
	LONG lRes = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &hKey);
	if (lRes == ERROR_SUCCESS) {
		WCHAR szBuffer[512];
		DWORD dwBufferSize = sizeof(szBuffer);
		lRes = RegQueryValueExW(hKey, L"ProductName", 0, nullptr, (LPBYTE)szBuffer, &dwBufferSize);
		if (lRes == ERROR_SUCCESS)
			return wcsncmp(szBuffer, L"Windows 10", 10) == 0;
		RegCloseKey(hKey);
	}
	return false;
}

// Progress function, to be called regularly to update the progress.
typedef std::function<void (const char * /* message */, unsigned /* progress */)> ProgressFn;

void fix_model_by_win10_sdk(const std::string &path_src, const std::string &path_dst, ProgressFn on_progress, ThrowOnCancelFn throw_on_cancel)
{
	if (! is_windows10())
		throw std::runtime_error("fix_model_by_win10_sdk called on non Windows 10 system");

	if (! winrt_load_runtime_object_library())
		throw std::runtime_error("Failed to initialize the WinRT library.");

	HRESULT hr = (*s_RoInitialize)(RO_INIT_MULTITHREADED);
	{
		on_progress(L("Exporting the source model"), 20);

		Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream>       fileStream;
		hr = winrt_open_file_stream(boost::nowide::widen(path_src), ABI::Windows::Storage::FileAccessMode::FileAccessMode_Read, fileStream.GetAddressOf(), throw_on_cancel);

		Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Printing3D::IPrinting3D3MFPackage> printing3d3mfpackage;
		hr = winrt_activate_instance(L"Windows.Graphics.Printing3D.Printing3D3MFPackage", printing3d3mfpackage.GetAddressOf());

		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Graphics::Printing3D::Printing3DModel*>> modelAsync;
		hr = printing3d3mfpackage->LoadModelFromPackageAsync(fileStream.Get(), modelAsync.GetAddressOf());

		AsyncStatus status = winrt_async_await(modelAsync, throw_on_cancel);
		Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Printing3D::IPrinting3DModel>	  model;
		if (status == AsyncStatus::Completed)
			hr = modelAsync->GetResults(model.GetAddressOf());
		else
			throw std::runtime_error(L("Failed loading the input model."));

		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::Collections::IVector<ABI::Windows::Graphics::Printing3D::Printing3DMesh*>> meshes;
		hr = model->get_Meshes(meshes.GetAddressOf());
		unsigned num_meshes = 0;
		hr = meshes->get_Size(&num_meshes);
		
		on_progress(L("Repairing the model by the Netfabb service"), 40);
		
		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction>					  repairAsync;
		hr = model->RepairAsync(repairAsync.GetAddressOf());
		status = winrt_async_await(repairAsync, throw_on_cancel);
		if (status != AsyncStatus::Completed)
			throw std::runtime_error(L("Mesh repair failed."));
		repairAsync->GetResults();

		on_progress(L("Loading the repaired model"), 60);

		// Verify the number of meshes returned after the repair action.
		meshes.Reset();
		hr = model->get_Meshes(meshes.GetAddressOf());
		hr = meshes->get_Size(&num_meshes);

		// Save model to this class' Printing3D3MFPackage.
		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction>					  saveToPackageAsync;
		hr = printing3d3mfpackage->SaveModelToPackageAsync(model.Get(), saveToPackageAsync.GetAddressOf());
		status = winrt_async_await(saveToPackageAsync, throw_on_cancel);
		if (status != AsyncStatus::Completed)
			throw std::runtime_error(L("Saving mesh into the 3MF container failed."));
		hr = saveToPackageAsync->GetResults();

		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::Streams::IRandomAccessStream*>> generatorStreamAsync;
		hr = printing3d3mfpackage->SaveAsync(generatorStreamAsync.GetAddressOf());
		status = winrt_async_await(generatorStreamAsync, throw_on_cancel);
		if (status != AsyncStatus::Completed)
			throw std::runtime_error(L("Saving mesh into the 3MF container failed."));
		Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream> generatorStream;
		hr = generatorStreamAsync->GetResults(generatorStream.GetAddressOf());

		// Go to the beginning of the stream.
		generatorStream->Seek(0);
		Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IInputStream> inputStream;
		hr = generatorStream.As(&inputStream);

		// Get the buffer factory.
		Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IBufferFactory> bufferFactory;
		hr = winrt_get_activation_factory(L"Windows.Storage.Streams.Buffer", bufferFactory.GetAddressOf());

		// Open the destination file.
		FILE *fout = boost::nowide::fopen(path_dst.c_str(), "wb");

		Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IBuffer> buffer;
		byte														   *buffer_ptr;
		bufferFactory->Create(65536 * 2048, buffer.GetAddressOf());
		{
			Microsoft::WRL::ComPtr<Windows::Storage::Streams::IBufferByteAccess> bufferByteAccess;
			buffer.As(&bufferByteAccess);
			hr = bufferByteAccess->Buffer(&buffer_ptr);
		}
		uint32_t length;
		hr = buffer->get_Length(&length);

		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperationWithProgress<ABI::Windows::Storage::Streams::IBuffer*, UINT32>> asyncRead;
		for (;;) {
			hr = inputStream->ReadAsync(buffer.Get(), 65536 * 2048, ABI::Windows::Storage::Streams::InputStreamOptions_ReadAhead, asyncRead.GetAddressOf());
			status = winrt_async_await(asyncRead, throw_on_cancel);
			if (status != AsyncStatus::Completed)
				throw std::runtime_error(L("Saving mesh into the 3MF container failed."));
			hr = buffer->get_Length(&length);
			if (length == 0)
				break;
			fwrite(buffer_ptr, length, 1, fout);
		}
		fclose(fout);
		// Here all the COM objects will be released through the ComPtr destructors.
	}
	(*s_RoUninitialize)();
}

class RepairCanceledException : public std::exception {
public:
   const char* what() const throw() { return "Model repair has been canceled"; }
};

void fix_model_by_win10_sdk_gui(const ModelObject &model_object, const Print &print, Model &result)
{
	std::mutex 						mutex;
	std::condition_variable			condition;
	std::unique_lock<std::mutex>	lock(mutex);
	struct Progress {
		std::string 				message;
		int 						percent  = 0;
		bool						updated = false;
	} progress;
	std::atomic<bool>				canceled = false;
	std::atomic<bool>				finished = false;

	// Open a progress dialog.
	wxProgressDialog progress_dialog(
		_(L("Model fixing")),
		_(L("Exporting model...")),
		100, nullptr, wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT);
	// Executing the calculation in a background thread, so that the COM context could be created with its own threading model.
	// (It seems like wxWidgets initialize the COM contex as single threaded and we need a multi-threaded context).
	bool success  = false;
	auto on_progress = [&mutex, &condition, &progress](const char *msg, unsigned prcnt) {
        std::lock_guard<std::mutex> lk(mutex);
		progress.message = msg;
		progress.percent = prcnt;
		progress.updated = true;
	    condition.notify_all();
	};
	auto worker_thread = boost::thread([&model_object, &print, &result, on_progress, &success, &canceled, &finished]() {
		try {
			on_progress(L("Exporting the source model"), 0);
			boost::filesystem::path path_src = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
			path_src += ".3mf";
			Model model;
			model.add_object(model_object);
			if (! Slic3r::store_3mf(path_src.string().c_str(), &model, const_cast<Print*>(&print), false)) {
				boost::filesystem::remove(path_src);
				throw std::runtime_error(L("Export of a temporary 3mf file failed"));
			}
			model.clear_objects(); 
			model.clear_materials();
			boost::filesystem::path path_dst = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
			path_dst += ".3mf";
			fix_model_by_win10_sdk(path_src.string().c_str(), path_dst.string(), on_progress, 
				[&canceled]() { if (canceled) throw RepairCanceledException(); });
			boost::filesystem::remove(path_src);
			PresetBundle bundle;
			on_progress(L("Loading the repaired model"), 80);
		    bool loaded = Slic3r::load_3mf(path_dst.string().c_str(), &bundle, &result);
			boost::filesystem::remove(path_dst);
			if (! loaded)
 				throw std::runtime_error(L("Import of the repaired 3mf file failed"));
			success  = true;
			finished = true;
			on_progress(L("Model repair finished"), 100);
		} catch (RepairCanceledException &ex) {
			canceled = true;
			finished = true;
			on_progress(L("Model repair canceled"), 100);
		} catch (std::exception &ex) {
			success = false;
			finished = true;
			on_progress(ex.what(), 100);
		}
	});
    while (! finished) {
		condition.wait_for(lock, std::chrono::milliseconds(500), [&progress]{ return progress.updated; });
		if (! progress_dialog.Update(progress.percent, _(progress.message)))
			canceled = true;
		progress.updated = false;
    }

	if (canceled) {
		// Nothing to show.
	} else if (success) {
		wxMessageDialog dlg(nullptr, _(L("Model repaired successfully")), _(L("Model Repair by the Netfabb service")), wxICON_INFORMATION | wxOK_DEFAULT);
		dlg.ShowModal();
	} else {
		wxMessageDialog dlg(nullptr, _(L("Model repair failed: \n")) + _(progress.message), _(L("Model Repair by the Netfabb service")), wxICON_ERROR | wxOK_DEFAULT);
		dlg.ShowModal();
	}
	worker_thread.join();
}

} // namespace Slic3r

#endif /* HAS_WIN10SDK */

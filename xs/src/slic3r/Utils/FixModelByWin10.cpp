#ifdef HAS_WIN10SDK

#ifndef NOMINMAX
# define NOMINMAX
#endif

#include "FixModelByWin10.hpp"

#include <roapi.h>
#include <string>
#include <cstdint>
#include <thread>
// for ComPtr
#include <wrl/client.h>
// from C:/Program Files (x86)/Windows Kits/10/Include/10.0.17134.0/
#include <winrt/robuffer.h>
#include <winrt/windows.storage.provider.h>
#include <winrt/windows.graphics.printing3d.h>

#include <boost/filesystem.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Format/3mf.hpp"
#include "../GUI/GUI.hpp"
#include "../GUI/PresetBundle.hpp"

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

template<typename T>
static AsyncStatus winrt_async_await(const Microsoft::WRL::ComPtr<T> &asyncAction, int blocking_tick_ms = 300)
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
		::Sleep(blocking_tick_ms);
	}
}

static HRESULT winrt_open_file_stream(
	const std::wstring									 &path,
	ABI::Windows::Storage::FileAccessMode				  mode,
	ABI::Windows::Storage::Streams::IRandomAccessStream **fileStream)
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
	AsyncStatus status = winrt_async_await(fileOpenAsync);
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

	status = winrt_async_await(fileStreamAsync);
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

bool fix_model_by_win10_sdk(const std::string &path_src, const std::string &path_dst)
{
	if (! is_windows10()) {
		return false;
	}

	if (! winrt_load_runtime_object_library()) {
		printf("Failed to initialize the WinRT library. This should not happen on Windows 10. Exiting.\n");
		return false;
	}

	HRESULT hr = (*s_RoInitialize)(RO_INIT_MULTITHREADED);
	{
		Microsoft::WRL::ComPtr<ABI::Windows::Storage::Streams::IRandomAccessStream>       fileStream;
		hr = winrt_open_file_stream(boost::nowide::widen(path_src), ABI::Windows::Storage::FileAccessMode::FileAccessMode_Read, fileStream.GetAddressOf());

		Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Printing3D::IPrinting3D3MFPackage> printing3d3mfpackage;
		hr = winrt_activate_instance(L"Windows.Graphics.Printing3D.Printing3D3MFPackage", printing3d3mfpackage.GetAddressOf());

		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Graphics::Printing3D::Printing3DModel*>> modelAsync;
		hr = printing3d3mfpackage->LoadModelFromPackageAsync(fileStream.Get(), modelAsync.GetAddressOf());

		AsyncStatus status = winrt_async_await(modelAsync);
		Microsoft::WRL::ComPtr<ABI::Windows::Graphics::Printing3D::IPrinting3DModel>	  model;
		if (status == AsyncStatus::Completed) {
			hr = modelAsync->GetResults(model.GetAddressOf());
		} else {
			printf("Failed loading the input model. Exiting.\n");
			return false;
		}

		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::Collections::IVector<ABI::Windows::Graphics::Printing3D::Printing3DMesh*>> meshes;
		hr = model->get_Meshes(meshes.GetAddressOf());
		unsigned num_meshes = 0;
		hr = meshes->get_Size(&num_meshes);
		
		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction>					  repairAsync;
		hr = model->RepairAsync(repairAsync.GetAddressOf());
		status = winrt_async_await(repairAsync);
		if (status != AsyncStatus::Completed) {
			printf("Mesh repair failed. Exiting.\n");
			return false;
		}
		printf("Mesh repair finished successfully.\n");
		repairAsync->GetResults();

		// Verify the number of meshes returned after the repair action.
		meshes.Reset();
		hr = model->get_Meshes(meshes.GetAddressOf());
		hr = meshes->get_Size(&num_meshes);

		// Save model to this class' Printing3D3MFPackage.
		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction>					  saveToPackageAsync;
		hr = printing3d3mfpackage->SaveModelToPackageAsync(model.Get(), saveToPackageAsync.GetAddressOf());
		status = winrt_async_await(saveToPackageAsync);
		if (status != AsyncStatus::Completed) {
			printf("Saving mesh into the 3MF container failed. Exiting.\n");
			return false;
		}
		hr = saveToPackageAsync->GetResults();

		Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncOperation<ABI::Windows::Storage::Streams::IRandomAccessStream*>> generatorStreamAsync;
		hr = printing3d3mfpackage->SaveAsync(generatorStreamAsync.GetAddressOf());
		status = winrt_async_await(generatorStreamAsync);
		if (status != AsyncStatus::Completed) {
			printf("Saving mesh into the 3MF container failed. Exiting.\n");
			return false;
		}
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
			status = winrt_async_await(asyncRead);
			if (status != AsyncStatus::Completed) {
				printf("Saving mesh into the 3MF container failed. Exiting.\n");
				return false;
			}
			hr = buffer->get_Length(&length);
			if (length == 0)
				break;
			fwrite(buffer_ptr, length, 1, fout);
		}
		fclose(fout);
		// Here all the COM objects will be released through the ComPtr destructors.
	}
	(*s_RoUninitialize)();
	return true;
}

void fix_model_by_win10_sdk_gui(const ModelObject &model_object, const Print &print, Model &result)
{
	enum { PROGRESS_RANGE = 1000 };
	wxProgressDialog progress_dialog(
		_(L("Model fixing")),
		_(L("Exporting model...")),
		PROGRESS_RANGE, nullptr, wxPD_AUTO_HIDE | wxPD_APP_MODAL | wxPD_CAN_ABORT);
	progress_dialog.Pulse();

	// Executing the calculation in a background thread, so that the COM context could be created with its own threading model.
	// (It seems like wxWidgets initialize the COM contex as single threaded and we need a multi-threaded context).
	auto worker = std::thread([&model_object, &print, &result, &progress_dialog](){
		boost::filesystem::path path_src = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
		path_src += ".3mf";
		Model model;
		model.add_object(model_object);
		bool res = Slic3r::store_3mf(path_src.string().c_str(), &model, const_cast<Print*>(&print), false);
		model.clear_objects(); 
		model.clear_materials();

		boost::filesystem::path path_dst = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path();
		path_dst += ".3mf";
		res = fix_model_by_win10_sdk(path_src.string().c_str(), path_dst.string());
		boost::filesystem::remove(path_src);
		PresetBundle bundle;
	    res = Slic3r::load_3mf(path_dst.string().c_str(), &bundle, &result);
		boost::filesystem::remove(path_dst);
	});
	worker.join();
}

} // namespace Slic3r

#endif /* HAS_WIN10SDK */

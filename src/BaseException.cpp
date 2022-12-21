#include "BaseException.h"
#include <iomanip>
#include <string>
#include <sstream>
#include <iostream>
#include <mutex>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>
#include <boost/log/trivial.hpp>
#include <boost/format.hpp>

static std::string g_log_folder;
static std::atomic<int> g_crash_log_count = 0;
static std::mutex g_dump_mutex;

CBaseException::CBaseException(HANDLE hProcess, WORD wPID, LPCTSTR lpSymbolPath, PEXCEPTION_POINTERS pEp):
	CStackWalker(hProcess, wPID, lpSymbolPath)
{
	if (NULL != pEp)
	{
		m_pEp = new EXCEPTION_POINTERS;
		CopyMemory(m_pEp, pEp, sizeof(EXCEPTION_POINTERS));
	}
	output_file = new boost::nowide::ofstream();
	std::time_t t = std::time(0);
	std::tm* now_time = std::localtime(&t);
	std::stringstream buf;

	if (!g_log_folder.empty()) {
		buf << std::put_time(now_time, "crash_%a_%b_%d_%H_%M_%S_") <<g_crash_log_count++ <<".log";
		auto log_folder = (boost::filesystem::path(g_log_folder) / "log").make_preferred();
		if (!boost::filesystem::exists(log_folder)) {
		    boost::filesystem::create_directory(log_folder);
	    }
		auto crash_log_path = boost::filesystem::path(log_folder / buf.str()).make_preferred();
		std::string log_filename = crash_log_path.string();
		output_file->open(log_filename, std::ios::out | std::ios::app);
	}
}

CBaseException::~CBaseException(void)
{
	if (output_file) {
		output_file->close();
		delete output_file;
	}
}


//BBS set crash log folder
void CBaseException::set_log_folder(std::string log_folder)
{
	g_log_folder = log_folder;
}

void CBaseException::OutputString(LPCTSTR lpszFormat, ...)
{
	TCHAR szBuf[2048] = _T("");
	va_list args;
	va_start(args, lpszFormat);
	_vsntprintf_s(szBuf, 2048, lpszFormat, args);
	va_end(args);

	//WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), szBuf, _tcslen(szBuf), NULL, NULL);

	//output it to the current directory of binary
	std::string output_str = textconv_helper::T2A_(szBuf);
	*output_file << output_str;
	output_file->flush();
}

void CBaseException::ShowLoadModules()
{
	LoadSymbol();
	LPMODULE_INFO pHead = GetLoadModules();
	LPMODULE_INFO pmi = pHead;

	TCHAR szBuf[MAX_COMPUTERNAME_LENGTH] = _T("");
	DWORD dwSize = MAX_COMPUTERNAME_LENGTH;
	GetUserName(szBuf, &dwSize);
	OutputString(_T("Current User:%s\r\n"), szBuf);
	OutputString(_T("BaseAddress:\tSize:\tName\tPath\tSymbolPath\tVersion\r\n"));
	while (NULL != pmi)
	{
		OutputString(_T("%08x\t%d\t%s\t%s\t%s\t%s\r\n"), (unsigned long)(pmi->ModuleAddress), pmi->dwModSize, pmi->szModuleName, pmi->szModulePath, pmi->szSymbolPath, pmi->szVersion);
		pmi = pmi->pNext;
	}

	FreeModuleInformations(pHead);
}

void CBaseException::ShowCallstack(HANDLE hThread, const CONTEXT* context)
{
	OutputString(_T("Show CallStack:\r\n"));
	LPSTACKINFO phead = StackWalker(hThread, context);
	FreeStackInformations(phead);
}

void CBaseException::ShowExceptionResoult(DWORD dwExceptionCode)
{
	OutputString(_T("Exception Code :%08x "), dwExceptionCode);
// BBS: to be checked
#if 1
	switch (dwExceptionCode)
	{
	case EXCEPTION_ACCESS_VIOLATION:
		{
			//OutputString(_T("ACCESS_VIOLATION(%s)\r\n"), _T("读写非法内存"));
			OutputString(_T("ACCESS_VIOLATION\r\n"));
		}
		return ;
	case EXCEPTION_DATATYPE_MISALIGNMENT:
		{
			//OutputString(_T("DATATYPE_MISALIGNMENT(%s)\r\n"), _T("线程视图在不支持对齐的硬件上读写未对齐的数据"));
			OutputString(_T("DATATYPE_MISALIGNMENT\r\n"));
		}
		return ;
	case EXCEPTION_BREAKPOINT:
		{
			//OutputString(_T("BREAKPOINT(%s)\r\n"), _T("遇到一个断点"));
			OutputString(_T("BREAKPOINT\r\n"));
		}
		return ;
	case EXCEPTION_SINGLE_STEP:
		{
			//OutputString(_T("SINGLE_STEP(%s)\r\n"), _T("单步")); //一般是发生在调试事件中
			OutputString(_T("SINGLE_STEP\r\n"));
		}
		return ;
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		{
			//OutputString(_T("ARRAY_BOUNDS_EXCEEDED(%s)\r\n"), _T("数组访问越界"));
			OutputString(_T("ARRAY_BOUNDS_EXCEEDED\r\n"));
		}
		return ;
	case EXCEPTION_FLT_DENORMAL_OPERAND:
		{
			//OutputString(_T("FLT_DENORMAL_OPERAND(%s)\r\n"), _T("浮点操作的一个操作数不正规，给定的浮点数无法表示")); //操作数的问题
			OutputString(_T("FLT_DENORMAL_OPERAND\r\n"));
		}
		return ;
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:
		{
			//OutputString(_T("FLT_DIVIDE_BY_ZERO(%s)\r\n"), _T("浮点数除0操作"));
			OutputString(_T("FLT_DIVIDE_BY_ZERO\r\n"));
		}
		return ;
	case EXCEPTION_FLT_INEXACT_RESULT:
		{
			//OutputString(_T("FLT_INEXACT_RESULT(%s)\r\n"), _T("浮点数操作的结果无法表示")); //无法表示一般是数据太小，超过浮点数表示的范围, 计算之后产生的结果异常
			OutputString(_T("FLT_INEXACT_RESULT\r\n"));
		}
		return ;
	case EXCEPTION_FLT_INVALID_OPERATION:
		{
			//OutputString(_T("FLT_INVALID_OPERATION(%s)\r\n"), _T("其他浮点数异常"));
			OutputString(_T("FLT_INVALID_OPERATION\r\n"));
		}
		return ;
	case EXCEPTION_FLT_OVERFLOW:
		{
			//OutputString(_T("FLT_OVERFLOW(%s)\r\n"), _T("浮点操作的指数超过了相应类型的最大值"));
			OutputString(_T("FLT_OVERFLOW\r\n"));
		}
		return ;
	case EXCEPTION_FLT_STACK_CHECK:
		{
			//OutputString(_T("STACK_CHECK(%s)\r\n"), _T("栈越界或者栈向下溢出"));
			OutputString(_T("STACK_CHECK\r\n"));
		}
		return ;
	case EXCEPTION_INT_DIVIDE_BY_ZERO:
		{
			//OutputString(_T("INT_DIVIDE_BY_ZERO(%s)\r\n"), _T("整数除0异常"));
			OutputString(_T("INT_DIVIDE_BY_ZERO\r\n"));
		}
		return ;
	case EXCEPTION_INVALID_HANDLE:
		{
			//OutputString(_T("INVALID_HANDLE(%s)\r\n"), _T("句柄无效"));
			OutputString(_T("INVALID_HANDLE\r\n"));
		}
		return ;
	case EXCEPTION_PRIV_INSTRUCTION:
		{
			//OutputString(_T("PRIV_INSTRUCTION(%s)\r\n"), _T("线程试图执行当前机器模式不支持的指令"));
			OutputString(_T("PRIV_INSTRUCTION\r\n"));
		}
		return ;
	case EXCEPTION_IN_PAGE_ERROR:
		{
			//OutputString(_T("IN_PAGE_ERROR(%s)\r\n"), _T("线程视图访问未加载的虚拟内存页或者不能加载的虚拟内存页"));
			OutputString(_T("IN_PAGE_ERROR\r\n"));
		}
		return ;
	case EXCEPTION_ILLEGAL_INSTRUCTION:
		{
			//OutputString(_T("ILLEGAL_INSTRUCTION(%s)\r\n"), _T("线程视图执行无效指令"));
			OutputString(_T("ILLEGAL_INSTRUCTION\r\n"));
		}
		return ;
	case EXCEPTION_NONCONTINUABLE_EXCEPTION:
		{
			//OutputString(_T("NONCONTINUABLE_EXCEPTION(%s)\r\n"), _T("线程试图在一个不可继续执行的异常发生后继续执行"));
			OutputString(_T("NONCONTINUABLE_EXCEPTION\r\n"));
		}
		return ;
	case EXCEPTION_STACK_OVERFLOW:
		{
			//OutputString(_T("STACK_OVERFLOW(%s)\r\n"), _T("栈溢出"));
			OutputString(_T("STACK_OVERFLOW\r\n"));
		}
		return ;
	case EXCEPTION_INVALID_DISPOSITION:
		{
			//OutputString(_T("INVALID_DISPOSITION(%s)\r\n"), _T("异常处理程序给异常调度器返回了一个无效配置")); //使用高级语言编写的程序永远不会遇到这个异常
			OutputString(_T("INVALID_DISPOSITION\r\n"));
		}
		return ;
	case EXCEPTION_FLT_UNDERFLOW:
		{
			//OutputString(_T("FLT_UNDERFLOW(%s)\r\n"), _T("浮点数操作的指数小于相应类型的最小值"));
			OutputString(_T("FLT_UNDERFLOW\r\n"));
		}
		return ;
	case EXCEPTION_INT_OVERFLOW:
		{
			//OutputString(_T("INT_OVERFLOW(%s)\r\n"), _T("整数操作越界"));
			OutputString(_T("INT_OVERFLOW\r\n"));
		}
		return ;
	}

	TCHAR szBuffer[512] = { 0 };

	FormatMessage(  FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_HMODULE,
		GetModuleHandle( _T("NTDLL.DLL") ),
		dwExceptionCode, 0, szBuffer, sizeof( szBuffer ), 0 );

	OutputString(_T("%s"), szBuffer);
	OutputString(_T("\r\n"));
#endif
}

LONG WINAPI CBaseException::UnhandledExceptionFilter(PEXCEPTION_POINTERS pExceptionInfo )
{
	if (pExceptionInfo->ExceptionRecord->ExceptionCode < 0x80000000
		//BBS: Load project on computers with SDC may trigger this exception (in ShowModal()),
		//     It's not fatal and should be ignored, or there will be lots of meaningless crash logs
		|| pExceptionInfo->ExceptionRecord->ExceptionCode==0xe0434352)
		//BBS: ignore the exception when copy preset
		//|| pExceptionInfo->ExceptionRecord->ExceptionCode==0xe06d7363)
	{
		//BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": got an ExceptionCode %1%, skip it!") % pExceptionInfo->ExceptionRecord->ExceptionCode;
		return EXCEPTION_CONTINUE_SEARCH;
	}
    g_dump_mutex.lock();
	CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pExceptionInfo);
	base.ShowExceptionInformation();
    g_dump_mutex.unlock();

	return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI CBaseException::UnhandledExceptionFilter2(PEXCEPTION_POINTERS pExceptionInfo )
{
	CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pExceptionInfo);
	base.ShowExceptionInformation();

	return EXCEPTION_CONTINUE_SEARCH;
}

BOOL CBaseException::GetLogicalAddress(
	PVOID addr, PTSTR szModule, DWORD len, DWORD& section, DWORD& offset )
{
	MEMORY_BASIC_INFORMATION mbi;

	if ( !VirtualQuery( addr, &mbi, sizeof(mbi) ) )
		return FALSE;

	DWORD hMod = (DWORD)mbi.AllocationBase;

	if ( !GetModuleFileName( (HMODULE)hMod, szModule, len ) )
		return FALSE;

	if (!hMod)
		return FALSE;

	PIMAGE_DOS_HEADER pDosHdr = (PIMAGE_DOS_HEADER)hMod;
	PIMAGE_NT_HEADERS pNtHdr = (PIMAGE_NT_HEADERS)(hMod + pDosHdr->e_lfanew);
	PIMAGE_SECTION_HEADER pSection = IMAGE_FIRST_SECTION( pNtHdr );

	DWORD rva = (DWORD)addr - hMod;

	//计算当前地址在第几个节
	for (unsigned i = 0; i < pNtHdr->FileHeader.NumberOfSections; i++, pSection++ )
	{
		DWORD sectionStart = pSection->VirtualAddress;
		DWORD sectionEnd = sectionStart + max(pSection->SizeOfRawData, pSection->Misc.VirtualSize);

		if ( (rva >= sectionStart) && (rva <= sectionEnd) )
		{
			section = i+1;
			offset = rva - sectionStart;
			return TRUE;
		}
	}

	return FALSE;   // Should never get here!
}

void CBaseException::ShowRegistorInformation(PCONTEXT pCtx)
{
#ifdef _M_IX86  // Intel Only!
	OutputString( _T("\nRegisters:\r\n") );

	OutputString(_T("EAX:%08X\r\nEBX:%08X\r\nECX:%08X\r\nEDX:%08X\r\nESI:%08X\r\nEDI:%08X\r\n"),
		pCtx->Eax, pCtx->Ebx, pCtx->Ecx, pCtx->Edx, pCtx->Esi, pCtx->Edi );

	OutputString( _T("CS:EIP:%04X:%08X\r\n"), pCtx->SegCs, pCtx->Eip );
	OutputString( _T("SS:ESP:%04X:%08X  EBP:%08X\r\n"),pCtx->SegSs, pCtx->Esp, pCtx->Ebp );
	OutputString( _T("DS:%04X  ES:%04X  FS:%04X  GS:%04X\r\n"), pCtx->SegDs, pCtx->SegEs, pCtx->SegFs, pCtx->SegGs );
	OutputString( _T("Flags:%08X\r\n"), pCtx->EFlags );

#endif

	OutputString( _T("\r\n") );
}

void CBaseException::STF(unsigned int ui,  PEXCEPTION_POINTERS pEp)
{
	CBaseException base(GetCurrentProcess(), GetCurrentProcessId(), NULL, pEp);
	throw base;
}

void CBaseException::ShowExceptionInformation()
{
	OutputString(_T("Exceptions:\r\n"));
	ShowExceptionResoult(m_pEp->ExceptionRecord->ExceptionCode);

	OutputString(_T("Exception Flag :0x%x "), m_pEp->ExceptionRecord->ExceptionFlags);
	OutputString(_T("NumberParameters :%ld \n"), m_pEp->ExceptionRecord->NumberParameters);
	for (int i = 0; i < m_pEp->ExceptionRecord->NumberParameters; i++)
	{
		OutputString(_T("Param %d :0x%x \n"), i, m_pEp->ExceptionRecord->ExceptionInformation[i]);
	}
	OutputString(_T("Context :%p \n"), m_pEp->ContextRecord);
    OutputString(_T("ContextFlag : 0x%x, EFlags: 0x%x \n"), m_pEp->ContextRecord->ContextFlags, m_pEp->ContextRecord->EFlags);

	TCHAR szFaultingModule[MAX_PATH];
	DWORD section, offset;
	GetLogicalAddress(m_pEp->ExceptionRecord->ExceptionAddress, szFaultingModule, sizeof(szFaultingModule), section, offset );
	OutputString( _T("Fault address:  0x%X 0x%X:0x%X %s\r\n"), m_pEp->ExceptionRecord->ExceptionAddress, section, offset, szFaultingModule );

	ShowRegistorInformation(m_pEp->ContextRecord);

	ShowCallstack(GetCurrentThread(), m_pEp->ContextRecord);
}
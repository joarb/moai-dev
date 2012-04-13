//
// WinService.cpp
//
// $Id: //poco/1.4/Util/src/WinService.cpp#1 $
//
// Library: Util
// Package: Windows
// Module:  WinService
//
// Copyright (c) 2004-2006, Applied Informatics Software Engineering GmbH.
// and Contributors.
//
// Permission is hereby granted, free of charge, to any person or organization
// obtaining a copy of the software and accompanying documentation covered by
// this license (the "Software") to use, reproduce, display, distribute,
// execute, and transmit the Software, and to prepare derivative works of the
// Software, and to permit third-parties to whom the Software is furnished to
// do so, all subject to the following:
// 
// The copyright notices in the Software and this entire statement, including
// the above license grant, this restriction and the following disclaimer,
// must be included in all copies of the Software, in whole or in part, and
// all derivative works of the Software, unless such copies or derivative
// works are solely in the form of machine-executable object code generated by
// a source language processor.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
// SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
// FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
//


#include "Poco/Util/WinService.h"
#include "Poco/Thread.h"
#include "Poco/Exception.h"
#if defined(POCO_WIN32_UTF8)
#include "Poco/UnicodeConverter.h"
#endif


using Poco::Thread;
using Poco::SystemException;
using Poco::NotFoundException;
using Poco::OutOfMemoryException;


namespace Poco {
namespace Util {


const int WinService::STARTUP_TIMEOUT = 30000;


WinService::WinService(const std::string& name):
	_name(name),
	_svcHandle(0)
{
	_scmHandle = OpenSCManager(NULL, NULL, SC_MANAGER_ALL_ACCESS);
	if (!_scmHandle) throw SystemException("cannot open Service Control Manager");
}


WinService::~WinService()
{
	close();
	CloseServiceHandle(_scmHandle);
}


const std::string& WinService::name() const
{
	return _name;
}


std::string WinService::displayName() const
{
	POCO_LPQUERY_SERVICE_CONFIG pSvcConfig = config();
#if defined(POCO_WIN32_UTF8)
	std::wstring udispName(pSvcConfig->lpDisplayName);
	std::string dispName;
	Poco::UnicodeConverter::toUTF8(udispName, dispName);
#else
	std::string dispName(pSvcConfig->lpDisplayName);
#endif
	LocalFree(pSvcConfig);
	return dispName;
}


std::string WinService::path() const
{
	POCO_LPQUERY_SERVICE_CONFIG pSvcConfig = config();
#if defined(POCO_WIN32_UTF8)
	std::wstring upath(pSvcConfig->lpBinaryPathName);
	std::string path;
	UnicodeConverter::toUTF8(upath, path);
#else
	std::string path(pSvcConfig->lpBinaryPathName);
#endif
	LocalFree(pSvcConfig);
	return path;
}


void WinService::registerService(const std::string& path, const std::string& displayName)
{
	close();
#if defined(POCO_WIN32_UTF8)
	std::wstring uname;
	Poco::UnicodeConverter::toUTF16(_name, uname);
	std::wstring udisplayName;
	Poco::UnicodeConverter::toUTF16(displayName, udisplayName);
	std::wstring upath;
	Poco::UnicodeConverter::toUTF16(path, upath);
	_svcHandle = CreateServiceW(
		_scmHandle,
		uname.c_str(),
		udisplayName.c_str(), 
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_NORMAL,
		upath.c_str(),
		NULL, NULL, NULL, NULL, NULL);
#else
	_svcHandle = CreateService(
		_scmHandle,
		_name.c_str(),
		displayName.c_str(), 
		SERVICE_ALL_ACCESS,
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_DEMAND_START,
		SERVICE_ERROR_NORMAL,
		path.c_str(),
		NULL, NULL, NULL, NULL, NULL);
#endif
	if (!_svcHandle)
		throw SystemException("cannot register service", _name);
}

	
void WinService::registerService(const std::string& path)
{
	registerService(path, _name);
}


void WinService::unregisterService()
{
	open();
	if (!DeleteService(_svcHandle))
		throw SystemException("cannot unregister service", _name);
}


bool WinService::isRegistered() const
{
	return tryOpen();
}


bool WinService::isRunning() const
{
	open();
	SERVICE_STATUS ss;
	if (!QueryServiceStatus(_svcHandle, &ss))
		throw SystemException("cannot query service status", _name);
	return ss.dwCurrentState == SERVICE_RUNNING;
}

	
void WinService::start()
{
	open();
	if (!StartService(_svcHandle, 0, NULL))
		throw SystemException("cannot start service", _name);

	SERVICE_STATUS svcStatus;
	long msecs = 0;
	while (msecs < STARTUP_TIMEOUT)
	{
		if (!QueryServiceStatus(_svcHandle, &svcStatus)) break;
		if (svcStatus.dwCurrentState != SERVICE_START_PENDING) break;
		Thread::sleep(250);
		msecs += 250;
	}
	if (!QueryServiceStatus(_svcHandle, &svcStatus))
		throw SystemException("cannot query status of starting service", _name);
	else if (svcStatus.dwCurrentState != SERVICE_RUNNING)
		throw SystemException("service failed to start within a reasonable time", _name);
 }


void WinService::stop()
{
	open();
	SERVICE_STATUS svcStatus;
	if (!ControlService(_svcHandle, SERVICE_CONTROL_STOP, &svcStatus))
		throw SystemException("cannot stop service", _name);
}


void WinService::setStartup(WinService::Startup startup)
{
	open();
	DWORD startType;
	switch (startup)
	{
	case SVC_AUTO_START:
		startType = SERVICE_AUTO_START;
		break;
	case SVC_MANUAL_START:
		startType = SERVICE_DEMAND_START;
		break;
	case SVC_DISABLED:
		startType = SERVICE_DISABLED;
		break;
	default:
		startType = SERVICE_NO_CHANGE;
	}
	if (!ChangeServiceConfig(_svcHandle, SERVICE_NO_CHANGE, startType, SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL))
	{
		throw SystemException("cannot change service startup mode");
	}
}

	
WinService::Startup WinService::getStartup() const
{
	POCO_LPQUERY_SERVICE_CONFIG pSvcConfig = config();
	Startup result;
	switch (pSvcConfig->dwStartType)
	{
	case SERVICE_AUTO_START:
	case SERVICE_BOOT_START:
	case SERVICE_SYSTEM_START:
		result = SVC_AUTO_START;
		break;
	case SERVICE_DEMAND_START:
		result = SVC_MANUAL_START;
		break;
	case SERVICE_DISABLED:
		result = SVC_DISABLED;
		break;
	default:
		poco_debugger();
		result = SVC_MANUAL_START;
	}
	LocalFree(pSvcConfig);
	return result;
}


void WinService::open() const
{
	if (!tryOpen())
		throw NotFoundException("service does not exist", _name);
}


bool WinService::tryOpen() const
{
#if defined(POCO_WIN32_UTF8)
	std::wstring uname;
	Poco::UnicodeConverter::toUTF16(_name, uname);
	_svcHandle = OpenServiceW(_scmHandle, uname.c_str(), SERVICE_ALL_ACCESS);
#else
	_svcHandle = OpenService(_scmHandle, _name.c_str(), SERVICE_ALL_ACCESS);
#endif
	return _svcHandle != 0;
}


void WinService::close() const
{
	if (_svcHandle)
	{
		CloseServiceHandle(_svcHandle);
	}
}


POCO_LPQUERY_SERVICE_CONFIG WinService::config() const
{
	open();
	int size = 4096;
	DWORD bytesNeeded;
	POCO_LPQUERY_SERVICE_CONFIG pSvcConfig = (POCO_LPQUERY_SERVICE_CONFIG) LocalAlloc(LPTR, size);
	if (!pSvcConfig) throw OutOfMemoryException("cannot allocate service config buffer");
	try
	{
#if defined(POCO_WIN32_UTF8)
		while (!QueryServiceConfigW(_svcHandle, pSvcConfig, size, &bytesNeeded))
#else
		while (!QueryServiceConfig(_svcHandle, pSvcConfig, size, &bytesNeeded))
#endif
		{
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				LocalFree(pSvcConfig);
				size = bytesNeeded;
				pSvcConfig = (POCO_LPQUERY_SERVICE_CONFIG) LocalAlloc(LPTR, size);
			}
			else throw SystemException("cannot query service configuration", _name);
		}
	}
	catch (...)
	{
		LocalFree(pSvcConfig);
		throw;
	}
	return pSvcConfig;
}


} } // namespace Poco::Util

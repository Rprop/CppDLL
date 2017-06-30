/********************************************************************
	Created:	2012/05/19  19:20
	Filename: 	CppDLL.cpp
	Author:		rrrfff
	Url:	    http://blog.csdn.net/rrrfff
*********************************************************************/
#include "CppDLL_Class.h"

//-------------------------------------------------------------------------

void main()
{
	printf("请选择要分析的DLL..." RLIB_NEWLINEA);

	TCHAR szFiles[RLIB_DEFAULT_BUFFER_SIZE]; 
	szFiles[0]        = _T('\0');
	OPENFILENAME ofn  = { sizeof(OPENFILENAME) };
	ofn.lpstrTitle    = _T("选择C++动态链接库");
	ofn.lpstrFilter   = _T("动态链接库*.dll\0*.dll\0所有文件*.*\0*.*\0");
	ofn.lpstrFile     = szFiles;
	ofn.nMaxFile      = RLIB_COUNTOF_STR(szFiles);
	ofn.Flags         = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT;
	while (!GetOpenFileName(&ofn)) {
		printf("    请至少选择一个文件! error = 0x%.8lX" RLIB_NEWLINEA, CommDlgExtendedError());
		Thread::Sleep(1000);
	}
	printf("   可能需要花费一定时间, 请耐心等待..." RLIB_NEWLINEA);

	intptr_t count = 0;
	if (szFiles[ofn.nFileOffset - 1] != _T('\0')) {
		CppDLL cpp;
		if (cpp.Work(szFiles)) {
			printf("        %s" RLIB_NEWLINEA, RT2A(cpp.m_dll_path + _R("分析成功.")).toGBK());		
		} else {
			printf("        %s" RLIB_NEWLINEA, RT2A(cpp.m_dll_path + _R("分析失败, 非C++编写的DLL?")).toGBK());
		} //if
		count = 1;
	} else {
		static String common_dir;
		ManagedObject<ThreadPool> pool = pool.construct();
		if (pool.IsNull()) return;

		common_dir = Path::AddBackslash(szFiles); // 目录		
		TCHAR *pt  = szFiles + ofn.nFileOffset;
		while (*pt != _T('\0')) {
			pool->InvokeAndWait<TCHAR *>([](TCHAR *pt) {
				CppDLL cpp;
				if (cpp.Work(common_dir + pt)) {
					printf("        %s" RLIB_NEWLINEA, RT2A(cpp.m_dll_path + _R("分析成功.")).toGBK());
				} else {
					printf("        %s" RLIB_NEWLINEA, RT2A(cpp.m_dll_path + _R("分析失败, 非C++编写的DLL?") + (IO::File::TryDelete(cpp.m_dll_path) ? _R("【已删除】") : _R("【删除失败】"))).toGBK());
				} //if
			}, pt);

			pt += (TLEN(pt) + 1);
			++count;
		}

		pool->WaitForTasksComplete();
	} //if

	printf("   操作成功完成, %Id文件被分析." RLIB_NEWLINEA, count);
};
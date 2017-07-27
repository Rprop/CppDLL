/********************************************************************
	Created:	2012/05/19  18:17
	Filename: 	CppDLL_Class.h
	Author:		rrrfff
	Url:	    http://blog.csdn.net/rrrfff
*********************************************************************/
#include <RLib_LibImport.h>
#include "CppDLL_Resource.h"

//-------------------------------------------------------------------------

class CppDLL
{
private:
	enum EXPORTTYPE
	{
		TYPE_NAMESPACE = 0,
		TYPE_CLASS
	};

	struct Global
	{
	public:
		string undnamed;
		string mangled;

	public:
		Global() {};
		Global(const string &s1, const string &s2) {
			undnamed = s1;
			mangled  = s2;
		}
	};

	struct EXPORTINFO
	{
	public:
		string access;
		string type;
		string name;
		string param_other;
		string mangled;
		string oristr;
		EXPORTTYPE node_type;

	public:
		RLIB_DECLARE_DYNCREATE;

	public:
		bool operator <  (const EXPORTINFO &ei) const {
			return this->name < ei.name;
		}
		bool operator >  (const EXPORTINFO &ei) const {
			return this->name > ei.name;
		}
		bool operator == (const EXPORTINFO &ei) const {
			return (this->mangled == ei.mangled);
		}
	};

	struct VECTOR
	{
	public:
		string           name;
		List<VECTOR>     *children;
		List<EXPORTINFO> *element;

	public:
		VECTOR() {
			this->children = NULL;
			this->element  = NULL;
		}
		~VECTOR() {
			RLIB_Delete(this->children);
			RLIB_Delete(this->element);
		}
		RLIB_DECLARE_DYNCREATE;

	public:
		void TryInitChildren() {
			if (this->children == NULL)this->children = new List<VECTOR>;
		}
		void TryInitElement() {
			if(this->element == NULL) this->element = new List<EXPORTINFO>;
		}
		bool HasChildren() {
			return (this->children != NULL);
		}
		bool HasElement() {
			return (this->element != NULL);
		}
		bool IsClass() {
			if (HasElement() == false) { // 容器没有元素, 考虑是命名空间
				return false;
			} //if
			foreach(pe, (*this->element))
			{
				if (pe->node_type == TYPE_NAMESPACE) {
					return false;
				} //if
			}
			return true;
		}
	};

public:
	String           m_output_path;
	String           m_dll_path;
	List<String>     m_export_symbols_list;
	List<Global>     m_global_symbols_list;
	List<String>     m_undef_symbols_list;
	List<EXPORTINFO> m_class_element_list;
	VECTOR           m_tree_top;     
	UInt32           m_number_of_names;
	Integer          m_number_of_translated;
	Boolean          m_x64;

private:
	template<class HDR>
	intptr_t GetFileOffset(IO::FileStream *file, DWORD RVA, IMAGE_DOS_HEADER &dosHeader, HDR &ntHeaders)
	{
		LARGE_INTEGER        byteOffset;
		IMAGE_SECTION_HEADER sectionHeader;
		WORD c             = ntHeaders.FileHeader.NumberOfSections;
		byteOffset.LowPart = dosHeader.e_lfanew + sizeof(ntHeaders);
		file->Position     = static_cast<intptr_t>(byteOffset.LowPart);
		while (c-- && byteOffset.LowPart < ntHeaders.OptionalHeader.SizeOfHeaders - dosHeader.e_lfanew) {
			if (file->Read(&sectionHeader, sizeof(IMAGE_SECTION_HEADER)) > 0) {
				if (RVA >= sectionHeader.VirtualAddress && RVA < sectionHeader.VirtualAddress + sectionHeader.SizeOfRawData) {
					return static_cast<intptr_t>(RVA - sectionHeader.VirtualAddress + sectionHeader.PointerToRawData);
				} //if
			} else {
				break;
			} //if
		} //if
		return 0;
	}
	/// <summary>
	/// 分析导出表数据 忽略非C++风格导出函数
	/// </summary>
	intptr_t ReadAllExportSymbols(IO::FileStream *file)
	{
		IMAGE_DOS_HEADER dosHeader;
		if (file->Read(&dosHeader, sizeof(IMAGE_DOS_HEADER)) <= 0 ||
			dosHeader.e_magic != IMAGE_DOS_SIGNATURE) {
			return 0;
		} //if

		union
		{
			IMAGE_NT_HEADERS32 x86;
			IMAGE_NT_HEADERS64 x64;
		} ntHeaders;
		file->Position = dosHeader.e_lfanew;
		file->Read(&ntHeaders, sizeof(IMAGE_NT_HEADERS32));

		bool x64    = false;
		this->m_x64 = false;
		switch (ntHeaders.x86.OptionalHeader.Magic) {
		case IMAGE_NT_OPTIONAL_HDR64_MAGIC:
			file->Read(reinterpret_cast<LPBYTE>(&ntHeaders) + sizeof(IMAGE_NT_HEADERS32), 
					   sizeof(IMAGE_NT_HEADERS64) - sizeof(IMAGE_NT_HEADERS32));
			this->m_x64 = x64 = true;
		case IMAGE_NT_OPTIONAL_HDR32_MAGIC:
			break;
		default:
			return 0;
		}

		auto &expData  = (x64 ? ntHeaders.x64.OptionalHeader.DataDirectory : ntHeaders.x86.OptionalHeader.DataDirectory)[IMAGE_DIRECTORY_ENTRY_EXPORT];
		intptr_t count = 0;
		DWORD ExpRVA   = expData.VirtualAddress;
		DWORD ExpSize  = expData.Size;
		if (ExpRVA && ExpSize) {
			IMAGE_EXPORT_DIRECTORY expDir;
			file->Position = x64 ? 
				GetFileOffset(file, ExpRVA, dosHeader, ntHeaders.x64) : 
				GetFileOffset(file, ExpRVA, dosHeader, ntHeaders.x86);
			if (file->Read(&expDir, sizeof(IMAGE_EXPORT_DIRECTORY)) > 0) {					
				char export_name[RLIB_DEFAULT_BUFFER_SIZE], demangled[RLIB_COUNTOF(export_name)];
				intptr_t offsetOfNameRVA = x64 ? 
					GetFileOffset(file, expDir.AddressOfNames, dosHeader, ntHeaders.x64) : 
					GetFileOffset(file, expDir.AddressOfNames, dosHeader, ntHeaders.x86);
				this->m_number_of_names  = expDir.NumberOfNames;
				this->m_export_symbols_list.InitStorage(static_cast<intptr_t>(expDir.NumberOfNames));
				this->m_global_symbols_list.InitStorage(static_cast<intptr_t>(expDir.NumberOfNames / 2));
				this->m_undef_symbols_list.InitStorage(static_cast<intptr_t>(expDir.NumberOfNames / 2));
				this->m_class_element_list.InitStorage(static_cast<intptr_t>(expDir.NumberOfNames));

				DWORD dwNameRVA;
				for (DWORD i = 0; i < expDir.NumberOfNames; ++i, offsetOfNameRVA += 4) {					
					file->Position = offsetOfNameRVA;
					if (file->Read(&dwNameRVA, sizeof(dwNameRVA)) > 0) {
						file->Position = x64 ? 
							GetFileOffset(file, dwNameRVA, dosHeader, ntHeaders.x64) : 
							GetFileOffset(file, dwNameRVA, dosHeader, ntHeaders.x86);
						DWORD dwIndex = 0;
						while (file->Read(&export_name[dwIndex], 4) > 0) {
							if (export_name[dwIndex] == '\0' || export_name[dwIndex + 1] == '\0' ||
								export_name[dwIndex + 2] == '\0' || export_name[dwIndex + 3] == '\0') break;

							dwIndex += 4;
							static_assert(RLIB_COUNTOF(export_name) % 4 == 0, "BOOM");
							if (dwIndex >= RLIB_COUNTOF(export_name)) {
								assert(!"缓冲区太小");
								goto __continue_next;
							} //if
						}
						this->m_export_symbols_list.Add(export_name);
						if (export_name[0] != '?') {
							goto __continue_next;
						} //if

						extern char *rlib_unDName(char *buffer, const char *mangled, int buflen);
						auto demangled_name = rlib_unDName(demangled, export_name, RLIB_COUNTOF(demangled));
						if (demangled_name != NULL) {
							if (strstr(demangled_name, "`default") == NULL && 
								strstr(demangled_name, "`vftable") == NULL) {
								Analysis(demangled_name, export_name);
								++count;
							} //if
							if (demangled_name != demangled) AppBase::Collect(demangled_name);
						} //if
__continue_next:
						continue;
					} //if
				} //for
			} //if
		} //if
		
		return count;
	}
	/// <summary>
	/// 分析导出的名称
	/// </summary>
	void Analysis(string name, const string &mangled)
	{
		// 寻找访问修饰符
		if (name.IndexOf(_T("::")) == -1) {
			m_global_symbols_list.Add(Global(name, mangled));
			return;
		} //if
		
		EXPORTINFO ei;
		ei.mangled = mangled;
		ei.oristr  = name;
		intptr_t k = name.IndexOf(_T(": "));
		if (k == -1) {
			ei.node_type = TYPE_NAMESPACE;
		} else {
			ei.access = name.Substring(0, k);
			name.substring(k + 2);
		} //if
		k            = name.LastIndexOf(_T("("));
		bool is_func = (k != -1);
		if (is_func) {
			ei.param_other = name.Substring(k);
			name.substring(0, k);
		} //if
		intptr_t operator_index = name.IndexOfR(_T("::operator "));
		if (operator_index > 0) name.replace(_T(" "), _T("?"), operator_index + RLIB_COUNTOF_STR(_T("::operator")));
		k = name.LastIndexOf(_T(" "));
		if (name.IndexOf(_T("<")) == -1) {
			ei.type = name.Substring(0, k);
			ei.name = name.Substring(k + 1);
		} else {
			intptr_t left_count = -1;
			intptr_t white_char = -1;
			foreach(pc, name)
			{
				if (*pc == _T('<')) {
					left_count = left_count != -1 ? left_count + 1 : 1;
				} else if (*pc == _T('>')) {
					left_count--;
				} else if (*pc == _T(' ')) {
					if (left_count <= 0) white_char = i;
				} //if
			}
			ei.type = name.Substring(0, white_char);
			ei.name = name.Substring(ei.type.Length + 1);
		} //if
		if (operator_index > 0) ei.name.replace(_T("?"), _T(" "));
		if (ei.name.IsNull()) { // unrecognized
			m_global_symbols_list.Add(Global(_R("// ") + ei.oristr, mangled));
			return;
		} //if
		m_class_element_list.Add(ei);
	}
	/// <summary>
	/// 输出缩进空格
	/// </summary>
	void Tab(IO::FileStream *outfile, intptr_t current_tabsize)
	{
		while (--current_tabsize >= 0) {
			RLIB_StreamWriteA(outfile, " ");
		}
	}
	/// <summary>
	/// 按逻辑层次输出
	/// </summary>
	void Print(IO::FileStream *outfile, intptr_t &current_tabsize, VECTOR *current_pv)
	{
		if (current_pv->HasChildren())
		{		
			for (auto &v : (*current_pv->children)) // 遍历子节点
			{
				Tab(outfile, current_tabsize);
				if (v.IsClass()) {
					RLIB_StreamWriteA(outfile, "class ");
					GlobalizeString un(v.name);
					outfile->Write(un.toGBK(), un.sizeofGBK());
					RLIB_StreamWriteA(outfile, "\r\n");
					Tab(outfile, current_tabsize);
					RLIB_StreamWriteA(outfile, "{\r\n");
					Tab(outfile, current_tabsize);
					RLIB_StreamWriteA(outfile, "public: // guess member\r\n");
					Tab(outfile, current_tabsize + 4);
					RLIB_StreamWriteA(outfile, "TCHAR *unused[64];\r\n\r\n");
					goto __next;
				} else {
					RLIB_StreamWriteA(outfile, "namespace ");
				} //if
				RLIB_StreamWriteStringA(outfile, v.name);
				RLIB_StreamWriteA(outfile, "\r\n");
				Tab(outfile, current_tabsize);
				RLIB_StreamWriteA(outfile, "{\r\n");
__next:
				current_tabsize += 4;
				Print(outfile, current_tabsize, &v);
				current_tabsize -= 4;
				Tab(outfile, current_tabsize);
				RLIB_StreamWriteA(outfile, "};\r\n");
				if (current_tabsize == 0)
				{
					RLIB_StreamWriteA(outfile, "\r\n");
				} //if
			}
			delete current_pv->children;
			current_pv->children = NULL;
		} //if

		if (current_pv->HasElement())
		{
			current_pv->element->Sort();
			string current_access;
			// 输出子元素
			for (auto &e : (*current_pv->element))
			{
				if (!e.access.IsNullOrEmpty() && (current_access.IsNull() || current_access != e.access)) {
					if (!current_access.IsNull()) RLIB_StreamWriteA(outfile, "\r\n");

					current_tabsize -= 4;
					current_access   = e.access;
					Tab(outfile, current_tabsize);
					RLIB_StreamWriteStringA(outfile, e.access);
					RLIB_StreamWriteA(outfile, ":\r\n");
					current_tabsize += 4;
				} //if

				// 输出注释
				Tab(outfile, current_tabsize);
				RLIB_StreamWriteA(outfile, "/// <summary>\r\n");
				Tab(outfile, current_tabsize);
				RLIB_StreamWriteA(outfile, "/// ");
				RLIB_StreamWriteStringA(outfile, e.mangled);
				RLIB_StreamWriteA(outfile, "\r\n");

// 				Tab(outfile, current_tabsize);
// 				RLIB_StreamWriteA(outfile, "/// ");
// 				RLIB_StreamWriteStringA(outfile, (e.type + T("(*)") + e.param_other));
// 				RLIB_StreamWriteA(outfile, "\r\n");

				Tab(outfile, current_tabsize);
				RLIB_StreamWriteA(outfile, "/// </summary>\r\n");

				Tab(outfile, current_tabsize);
				e.type.replace(_T("__thiscall"), _T(""));
				if (!e.type.IsNullOrEmpty()) {
					RLIB_StreamWriteStringA(outfile, e.type);
					RLIB_StreamWriteA(outfile, " ");
				} //if
				RLIB_StreamWriteStringA(outfile, e.name);
				RLIB_StreamWriteStringA(outfile, e.param_other);
				RLIB_StreamWriteA(outfile, ";");
				RLIB_StreamWriteA(outfile, "\r\n");
			}
			delete current_pv->element;
			current_pv->element = NULL;
		} //if
	}
	/// <summary>
	/// 输出到指定文件
	/// </summary>
	void GenerateCppHeader()
	{
		Path   path(this->m_dll_path);
		String fn = StringReference(path.GetInfo().Fname);

		auto outfile = IO::File::Create(this->m_output_path + fn + _R(".h"), FileMode::CreateNew);
		RLIB_StreamWriteA(outfile,
						  "//\r\n// This File has been generated by CppDLL"
						  "\r\n// Copyright (c) 2017 rrrfff, rrrfff@foxmail.com"
						  "\r\n// http://blog.csdn.net/rrrfff"
						  "\r\n\r\n//===========================================================================\r\n\r\n");
		RLIB_StreamWriteA(outfile, "#pragma comment(lib, \"");
		RLIB_StreamWriteStringA(outfile, fn + _R(".lib"));
		RLIB_StreamWriteA(outfile, "\")\r\n\r\n//");
		{
			String count_str = _R(" File Path: ") + path.GetDosPath() + _R("\r\n//");
			count_str += _R(" Number Of Names: ") + this->m_number_of_names.ToString() + _R("\r\n//");
			count_str += _R(" Number Of Translated: ") + this->m_number_of_translated.ToString();
			RLIB_StreamWriteStringA(outfile, count_str);
		}
		RLIB_StreamWriteA(outfile, "\r\n\r\n");
		RLIB_StreamWriteA(outfile, "// 未知类型符号\r\n");
		{
			for(auto &v : m_undef_symbols_list) {
				RLIB_StreamWriteStringA(outfile, (_R("// ") + v));
				RLIB_StreamWriteA(outfile, "\r\n");
			}
			if (m_undef_symbols_list.Length != 0) {
				RLIB_StreamWriteA(outfile, "\r\n");
				m_undef_symbols_list.Clear();
			} else {
				RLIB_StreamWriteA(outfile, "// 没有任何未知类型符号\r\n\r\n");
			} //if
		}

		intptr_t curr_tabsize = 0;

		RLIB_StreamWriteA(outfile, "// 类型导出符号\r\n");
		Print(outfile, curr_tabsize, &m_tree_top);

		RLIB_StreamWriteA(outfile, "// 全局导出符号\r\n");
		foreach(pv, m_global_symbols_list)
		{
			RLIB_StreamWriteStringA(outfile, pv->undnamed.Replace(_T("__cdecl "), _T("")));
			RLIB_StreamWriteA(outfile, ";//");
			RLIB_StreamWriteStringA(outfile, pv->mangled);
			RLIB_StreamWriteA(outfile, "\r\n");
		}
		if (m_global_symbols_list.Length != 0) {
			RLIB_StreamWriteA(outfile, "\r\n");
			m_global_symbols_list.Clear();
		} else {
			RLIB_StreamWriteA(outfile, "// 未找到任何全局导出符号\r\n\r\n");
		} //if
		
		delete outfile;
	}
	void GenerateExportDef()
	{
		Path   path(this->m_dll_path);
		String fn = StringReference(path.GetInfo().Fname);

		auto outfile = IO::File::Create(this->m_output_path + fn + _R(".def"), FileMode::CreateNew);
		RLIB_StreamWriteA(outfile,
						  ";\r\n; This File has been generated by CppDLL"
						  "\r\n; Copyright (c) 2017 rrrfff, rrrfff@foxmail.com\r\n; http://blog.csdn.net/rrrfff"
						  "\r\n;\r\nLIBRARY ");
		RLIB_StreamWriteStringA(outfile, path.GetFileName());
		RLIB_StreamWriteA(outfile, "\r\nEXPORTS\r\n");
		for (auto &v : m_export_symbols_list) {
			RLIB_StreamWriteStringA(outfile, v);
			RLIB_StreamWriteA(outfile, "\r\n");
		}
		delete outfile;

		m_export_symbols_list.Clear();
	}
	void GenerateImportLib()
	{
		String linkerpath = IO::Path::ToNtPath(_R("link100.dll"));
		if (!File::Exist(linkerpath)) {
			auto file = IO::File::Create(linkerpath, FileMode::CreateNew, FileAccess::Write, FileAttributes::Hidden);
			if (file != NULL) {
				file->Write(link_exe, sizeof(link_exe));
				delete file;

				file = IO::File::Create(_R("mspdb100.dll"), FileMode::CreateNew, FileAccess::Write, FileAttributes::Hidden);
				if (file != NULL) {
					file->Write(mspdb100_dll, sizeof(mspdb100_dll));
					delete file;
				} //if
				file = IO::File::Create(_R("msvcr100.dll"), FileMode::CreateNew, FileAccess::Write, FileAttributes::Hidden);
				if (file != NULL) {
					file->Write(msvcr100_dll, sizeof(msvcr100_dll));
					delete file;
				} //if
			} else {
				printf("    无法生成所需文件: link.exe" RLIB_NEWLINEA);
				return;
			} //if
		} //if

		PROCESS_INFORMATION ProcessInformation = { 0 };
		STARTUPINFO start = { sizeof(start) };
		start.dwFlags     = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;   
		start.wShowWindow = SW_HIDE;   

		Path   path(this->m_dll_path);
		String fn        = StringReference(path.GetInfo().Fname);	
		String parameter = _R("LINK /LIB /MACHINE:");
		parameter       += this->m_x64 ? _R("X64 /DEF:\"") : _R("X86 /DEF:\"");
		parameter       += this->m_output_path + fn + _R(".def");
		parameter       += _R("\" /OUT:\"");
		parameter       += this->m_output_path + fn + _R(".lib");
		parameter       += _R("\"");
		if (!CreateProcess(linkerpath.Substring(4), parameter, NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, NULL, &start, &ProcessInformation)) {
			printf("    无法生成库文件, 命令行如下: %s" RLIB_NEWLINEA,
				   GlobalizeString(parameter).toGBK());
		} //if
	}
	/// <summary>
	/// 计算分割符
	/// </summary>
	intptr_t Calc(string &name, intptr_t begin = 0)
	{
		intptr_t left_count = -1;
		intptr_t white_char = -1;
		for(intptr_t i = begin; i < name.Length; ++i)
		{
			if (name[i] == _T('<')) {
				left_count = left_count != -1 ? left_count + 1 : 1;
			} else if (name[i] == _T('>')) {
				--left_count;
			} else if (i == name.Length - 1) {
				break;
			} else if (name[i] == _T(':') && name[i + 1] == _T(':')) {
				if (left_count <= 0) {
					white_char = i;
					break;
				} //if
			} //if
		}
		return white_char;
	}
	intptr_t CalcWhite(string &name, intptr_t begin = 0)
	{
		intptr_t left_count = -1;
		intptr_t white_char = -1;
		for(intptr_t i = begin; i < name.Length; ++i)
		{
			if (name[i] == _T('<')) {
				left_count = left_count != -1 ? left_count + 1 : 1;
			} else if (name[i] == _T('>')) {
				left_count--;
			} else if (i == name.Length - 1) {
				break;
			} else if (name[i] == _T(' ')) {
				if (left_count <= 0) {
					white_char = i;
					break;
				} //if
			} //if
		}
		return white_char;
	}
	/// <summary>
	/// 建立结构
	/// </summary>
	void Tree()
	{
		m_tree_top.TryInitChildren();
		
		// 处理导出符号
		for (auto &ei : m_class_element_list)
		{
			intptr_t k  = 0, v = 0;
			VECTOR *pvr = &m_tree_top;
			while((k = Calc(ei.name, k)) != -1)
			{
				String &&curr_name = ei.name.Substring(v, k - v);
				pvr->TryInitChildren();
				foreach(pc, (*pvr->children))
				{
					if (pc->name == curr_name) {
						pvr = pc;
						goto __do_next;
					} //if
				}
				// insert if not exsit
				{
					VECTOR vr;
					vr.name = curr_name;
					pvr->children->Add(vr);
					pvr = &pvr->children->Get(pvr->children->Length - 1);
				}
__do_next:
				k += 2;
				v  = k;
			}
			pvr->TryInitElement();
			ei.name.substring(v);
			pvr->element->Add(ei);
			continue;
		}

		m_class_element_list.Clear();
	}
	/// <summary>
	/// 调整顺序
	/// </summary>
	void Adjust()
	{
		auto class_list = m_class_element_list;

		for (auto &ei : m_class_element_list)
		{
			if (ei.param_other.IsNullOrEmpty()) continue;

			auto params = ei.param_other.Substring(1, ei.param_other.Length - 2).Split(_T(","), 1, 64);
			foreach(param, (*params))
			{
				// 如果参数中没有类型关系则忽略
				if (param->IndexOf(_T("enum")) == -1 && param->IndexOf(_T("class")) == -1 && 
					param->IndexOf(_T("struct")) == -1) {
					continue;
				} //if

				if (param->EndsWith(_T('*'))) {
					continue; // 忽略指针
				} //if

				intptr_t begin = param->IndexOfR(_T(" ")); assert(begin != -1);
				intptr_t end   = CalcWhite(*param, begin);
				string type    = param->Substring(begin, (end == -1) ? 0 : (end - begin));
				// 查找符号
				{
					intptr_t order = -1, orderfind = -1;
					for (auto &e : class_list)
					{
						if (e.name.IndexOf(type) != -1) {
							orderfind = i;
							if (order != -1) {
								goto __success_find;
							} //if
							continue;
						} //if

						if (e == ei) {
							order = i;
							if (orderfind != -1) {
								goto __success_find;
							} //if
						} //if
					}
					if (orderfind == -1) m_undef_symbols_list.Add(type);
					break;

__success_find:
					if (orderfind < order) {
						class_list.RemoveAt(order);
						class_list.InsertAt(orderfind, ei);
					} //if
				}
			}
			delete params;
		}

		m_class_element_list.Clear();
		for (auto &e : class_list) {
			m_class_element_list.Add(e);
		}
	}

public:
	RLIB_DECLARE_DYNCREATE;
	bool Work(string filename)
	{
		this->m_dll_path    = Path::ToDosPath(filename);
		this->m_output_path = Path::ToNtPath(_R("Output\\"));
		!Directory::Exist(this->m_output_path) && Directory::Create(this->m_output_path);

//		this->m_output_path += this->m_dll_path.Substring(this->m_dll_path.LastIndexOfR(_T("\\")));

		// 分析导出符号
		auto file = IO::File::Open(filename, FileMode::OpenExist, FileAccess::Read, FileShare::Read);
		if (file != nullptr) {
			this->m_number_of_translated = ReadAllExportSymbols(file);
			delete file;
		} else {
			return false;
		} //if
		
// 		if (this->m_number_of_translated <= 0) {
// 			return false;
// 		} //if

		// 调整结构
//		Adjust();
		Tree();

		// 输出文件
		GenerateCppHeader();
		GenerateExportDef();
		GenerateImportLib();

		return true;
	}
};

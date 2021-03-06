#include "import.h"
#include "filesystem/path.h"
#include "objects/array.h"
#include <sys/stat.h>
#ifdef DEBUG
#include <iostream>
#endif

using namespace std;

ImportStatus Importer::import(const String2 &currentPath, const Value *parts,
                              int size) {
	ImportStatus ret;
	ret.fileName    = String::from("<bad>");
	ret.res         = ImportStatus::BAD_IMPORT;
	ret.toHighlight = 0;

	auto             it = 0;
	filesystem::path p  = filesystem::path(currentPath->str());
	if(!p.exists()) {
		// we take current path from the bytecode source
		// file itself. so this can only happen in case of
		// the repl.
		p = filesystem::path::getcwd();
	} else {
		p = p.parent_path();
	}
	// if p is not a directory, make the iterator look like 1,
	// because all the conditions below returns it - 1
	if(!p.is_directory())
		it++;
	else {
		while(p.is_directory() && it != size) {
			p = p / filesystem::path(parts[it].toString()->str());
			it++;
		}
	}
	if(p.is_directory() && it == size) {
		// cout << "Unable to import whole folder '" << paths << "'!" << endl;
		ret.res         = ImportStatus::FOLDER_IMPORT;
		ret.toHighlight = size - 1;
		return ret;
	}
	p = filesystem::path(p.str() + ".n");
	if(!p.exists()) {
		// cout << "Unable to open file : '" << path_ << "'!" << endl;
		ret.res         = ImportStatus::FILE_NOT_FOUND;
		ret.fileName    = String::from(p.str().c_str());
		ret.toHighlight = it - 1;
		return ret;
	}
	if(!p.is_file()) {
		ret.res         = ImportStatus::BAD_IMPORT;
		ret.fileName    = String::from(p.str().c_str());
		ret.toHighlight = it - 1;
		return ret;
	}
#ifdef DEBUG
	cout << "File path generated : " << p.str() << endl;
#endif
	p                     = p.make_absolute();
	std::string absolutep = p.str();
	FILE *      f         = fopen(absolutep.c_str(), "r");
	if(f == NULL) {
		ret.res         = ImportStatus::FOPEN_ERROR;
		ret.fileName    = String::from(strerror(errno));
		ret.toHighlight = it - 1;
		return ret;
	}
	// if the whole path was resolved, it was a valid module
	// import, else, there are some parts we need to resolve
	// at runtime
	ret.res = it == size ? ImportStatus::IMPORT_SUCCESS
	                     : ImportStatus::PARTIAL_IMPORT;
	ret.fileName    = String::from(absolutep.c_str());
	ret.toHighlight = it;
#ifdef DEBUG
	cout << absolutep << " imported successfully!" << endl;
#endif
	fclose(f);
	return ret;
}

#define STRINGIZE2(x) #x
#define STRINGIZE(x) STRINGIZE2(x)
#include "paths.h"

std::string barbakan_zero::getRepoFolder()
{
#if defined (CORELIBFOLDERPATH)	
	std::string coreLibFolderPath = STRINGIZE(CORELIBFOLDERPATH);
	return coreLibFolderPath + "/../../";
#else 
	throw("Core lib folder path should be given as prepocessor flags. Cmake should have configured it automatically.");
#endif
}

std::string barbakan_zero::getBuildFolder()
{
	std::string repoFolder = getRepoFolder();
#if defined (NDEBUG)
	return repoFolder + "build/release/";
#else
	return repoFolder + "build/debug/";
#endif
}

std::string barbakan_zero::getBuildDataFolder()
{
	std::string buildTypeFolder = getBuildFolder();
	return buildTypeFolder + "data/";
}

std::string barbakan_zero::getBuildTestDataFolder()
{
	std::string buildTypeFolder = getBuildFolder();
	return buildTypeFolder + "test/data/";
}

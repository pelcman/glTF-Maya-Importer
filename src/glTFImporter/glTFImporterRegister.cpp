#ifdef _MSC_VER
#pragma warning(disable : 4819)
#endif

#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MObject.h>
#include <maya/MStatus.h>
#include <maya/MStreamUtils.h>
#include <maya/MString.h>

#include <string>

#include "glTFImporter.h"

#define VENDOR_NAME "glTF-Maya-Importer project"
#define PLUGIN_NAME "glTF-Maya-Importer"
#define PLUGIN_VERSION "0.1.0"

#define IMPORTER_NAME_GLTF "glTF Import"

const char* const gltfImportOptionScript = "glTFImporterOptions";
const char* const gltfImportDefaultOptions =
    "import_animations=1;"
    "import_blendshapes=1;"
    "import_skins=1;";

static void PrintTextLn(const std::string& str)
{
#if MAYA_API_VERSION >= 20180000
    MStreamUtils::stdOutStream() << str << std::endl;
#else
    std::cerr << str << std::endl;
#endif
}

static void ShowVersion()
{
    std::string showText;
    showText += PLUGIN_NAME;
    showText += " ";
    showText += "ver";
    showText += PLUGIN_VERSION;

    PrintTextLn(showText);
}

#ifdef _WIN32
__declspec(dllexport)
#endif // _WIN32
    MStatus initializePlugin(MObject obj)
{
    MFnPlugin plugin(obj, VENDOR_NAME, PLUGIN_VERSION, "Any");

    ShowVersion();

    MStatus status = plugin.registerFileTranslator(IMPORTER_NAME_GLTF, "none",
                                                   glTFImporter::creator,
                                                   (char*)gltfImportOptionScript,
                                                   (char*)gltfImportDefaultOptions);
    return status;
}

//////////////////////////////////////////////////////////////

#ifdef _WIN32
__declspec(dllexport)
#endif // _WIN32
    MStatus uninitializePlugin(MObject obj)
{
    MFnPlugin plugin(obj);

    MStatus status = plugin.deregisterFileTranslator(IMPORTER_NAME_GLTF);
    return status;
}

//////////////////////////////////////////////////////////////

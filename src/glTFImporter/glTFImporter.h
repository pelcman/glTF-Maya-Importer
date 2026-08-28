#pragma once
#ifndef __GLTF_IMPORTER_H__
#define __GLTF_IMPORTER_H__

#include <maya/MPxFileTranslator.h>

class glTFImporter : public MPxFileTranslator
{
public:
    glTFImporter();
    virtual ~glTFImporter();

    static void* creator();

    MStatus reader(const MFileObject& file,
                   const MString& optionsString,
                   FileAccessMode mode);

    bool haveReadMethod() const;
    bool haveWriteMethod() const;
    bool canBeOpened() const;
    MString defaultExtension() const;
    MFileKind identifyFile(const MFileObject& fileName,
                           const char* buffer,
                           short size) const;
    MString filter() const;
};

#endif // __GLTF_IMPORTER_H__

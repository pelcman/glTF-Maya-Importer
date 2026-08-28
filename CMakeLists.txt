cmake_minimum_required(VERSION 3.15)

#Use solution folders.
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project("glTF-Maya-Importer")

# -- options ----------------------------------------------------
option(GLTF_MAYA_IMPORTER_BUILD_WITH_DRACO          "Build with Draco"          ON)
option(GLTF_MAYA_IMPORTER_BUILD_MAYA_GLTF_IMPORTER  "Build Maya glTF Importer"  ON)

# Target Maya version (2022-2027). Used to locate the default Maya directory;
# override GLTF_MAYA_IMPORTER_MAYA_PATH directly for non-standard installs.
SET(GLTF_MAYA_IMPORTER_MAYA_VERSION "2024" CACHE STRING "Target Maya version (e.g. 2022...2027)")

if (WIN32)
    SET(GLTF_MAYA_IMPORTER_MAYA_PATH "C:/Program Files/Autodesk/Maya${GLTF_MAYA_IMPORTER_MAYA_VERSION}" CACHE PATH "Installed Maya Directory")
elseif(APPLE)
    SET(GLTF_MAYA_IMPORTER_MAYA_PATH "/Applications/Autodesk/maya${GLTF_MAYA_IMPORTER_MAYA_VERSION}" CACHE PATH "Installed Maya Directory")
elseif(UNIX)
    SET(GLTF_MAYA_IMPORTER_MAYA_PATH "/usr/autodesk/maya${GLTF_MAYA_IMPORTER_MAYA_VERSION}" CACHE PATH "Installed Maya Directory")
endif()

# ===============================================================

set(CMAKE_POSITION_INDEPENDENT_CODE On)

# -- draco setting ----------------------------------------------
if(GLTF_MAYA_IMPORTER_BUILD_WITH_DRACO)
    add_subdirectory(externals/draco)

    ## beautify ME!
    set_target_properties(draco PROPERTIES FOLDER Draco)
    set_target_properties(draco_attributes PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_attributes_dec PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_attributes_enc PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_attributes_pred_schemes_dec PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_attributes_pred_schemes_enc PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_decode PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_encode PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_mesh_dec PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_mesh_enc PROPERTIES FOLDER Draco)
    set_target_properties(draco_core PROPERTIES FOLDER Draco)
    set_target_properties(draco_core_bit_coders PROPERTIES FOLDER Draco)
    set_target_properties(draco_dec_config PROPERTIES FOLDER Draco)
    set_target_properties(draco_decoder PROPERTIES FOLDER Draco)
    set_target_properties(draco_enc_config PROPERTIES FOLDER Draco)
    set_target_properties(draco_encoder PROPERTIES FOLDER Draco)
    set_target_properties(draco_io PROPERTIES FOLDER Draco)
    set_target_properties(draco_mesh PROPERTIES FOLDER Draco)
    set_target_properties(draco_metadata PROPERTIES FOLDER Draco)
    set_target_properties(draco_metadata_dec PROPERTIES FOLDER Draco)
    set_target_properties(draco_metadata_enc PROPERTIES FOLDER Draco)
    set_target_properties(draco_point_cloud PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_point_cloud_dec PROPERTIES FOLDER Draco)
    set_target_properties(draco_compression_point_cloud_enc PROPERTIES FOLDER Draco)
    set_target_properties(draco_points_dec PROPERTIES FOLDER Draco)
    set_target_properties(draco_points_enc PROPERTIES FOLDER Draco)
    set_target_properties(dracodec PROPERTIES FOLDER Draco)
    set_target_properties(dracoenc PROPERTIES FOLDER Draco)

    set(DRACO_LIB draco)
    add_definitions(-DENABLE_BUILD_WITH_DRACO)

    set(DRACO_SRC_PATH externals/draco/src/)
endif()

if(GLTF_MAYA_IMPORTER_BUILD_MAYA_GLTF_IMPORTER)
    if (WIN32)
        SET(MAYA_INCLUDE_PATH ${GLTF_MAYA_IMPORTER_MAYA_PATH}/include/)
        SET(MAYA_LIBRARY_PATH ${GLTF_MAYA_IMPORTER_MAYA_PATH}/lib/)
    elseif(APPLE)
        SET(MAYA_INCLUDE_PATH ${GLTF_MAYA_IMPORTER_MAYA_PATH}/include/)
        SET(MAYA_LIBRARY_PATH ${GLTF_MAYA_IMPORTER_MAYA_PATH}/Maya.app/Contents/MacOS)
    elseif(UNIX)
        SET(MAYA_INCLUDE_PATH ${GLTF_MAYA_IMPORTER_MAYA_PATH}/include/)
        SET(MAYA_LIBRARY_PATH ${GLTF_MAYA_IMPORTER_MAYA_PATH}/lib/)
    endif()

    message(MAYA_INCLUDE_PATH: "${MAYA_INCLUDE_PATH}")
    message(MAYA_LIBRARY_PATH: "${MAYA_LIBRARY_PATH}")
endif()

# -- compile options, include and libraries -----------------

set (CMAKE_CXX_STANDARD 11)

include_directories(
    ${CMAKE_BINARY_DIR}
    src/
    externals/
    ${DRACO_SRC_PATH}
    ${MAYA_INCLUDE_PATH}
)

link_directories(${MAYA_LIBRARY_PATH})

# force define for maya header ifdef condition (building for makefile project)
if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_WIN32 -DNT_PLUGIN")
elseif(APPLE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DOSMac_")
elseif(UNIX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Dlinux -m64 -g -pthread -D_BOOL -DLINUX -DLINUX_64 -DFUNCPROTO -DREQUIRE_IOSTREAM -fPIC -Wno-deprecated -fno-gnu-keywords")
endif()

# -- glTF Importer for maya --------------------
if (GLTF_MAYA_IMPORTER_BUILD_MAYA_GLTF_IMPORTER)

    add_library( glTFImporter SHARED
        ./src/glTFImporter/glTFImporterRegister.cpp
        ./src/glTFImporter/glTFImporter.cpp
        ./src/glTFImporter/GltfDocument.cpp
        ./src/glTFImporter/DracoDecoder.cpp
    )

    target_link_libraries( glTFImporter
                        ${DRACO_LIB}
                        OpenMaya
                        OpenMayaAnim
                        OpenMayaUI
                        Foundation
    )
    SET_TARGET_PROPERTIES(glTFImporter PROPERTIES PREFIX "")
    if(WIN32)
      SET_TARGET_PROPERTIES(glTFImporter PROPERTIES SUFFIX ".mll")
    elseif(APPLE)
      SET_TARGET_PROPERTIES(glTFImporter PROPERTIES SUFFIX ".bundle")
    else()
      SET_TARGET_PROPERTIES(glTFImporter PROPERTIES SUFFIX ".so")
      SET_TARGET_PROPERTIES(glTFImporter PROPERTIES LINK_FLAGS "-shared -m64 -g -pthread -pipe -D_BOOL -DLINUX -DLINUX_64 -DFUNCPROTO -DREQUIRE_IOSTREAM -fPIC -Wno-deprecated -fno-gnu-keywords -Wl,-Bsymbolic")
    endif()

    # [VisualStudio] default startup
    if (WIN32)
    if (CMAKE_VERSION VERSION_GREATER 3.6.0)
        set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT glTFImporter)
    endif()
    endif()
endif()

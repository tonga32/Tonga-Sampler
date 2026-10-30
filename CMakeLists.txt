cmake_minimum_required(VERSION 3.22)
project(TongaSampler2 VERSION 2.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 7.0.12
    GIT_SHALLOW ON)

# ONNX Runtime (motor que corre el modelo de IA de los stems)
FetchContent_Declare(onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip)

FetchContent_MakeAvailable(JUCE onnxruntime)

# --- Encabezado y DLL de onnxruntime dentro de lo descargado ---
file(GLOB_RECURSE ORT_HEADERS "${onnxruntime_SOURCE_DIR}/*onnxruntime_c_api.h")
if(NOT ORT_HEADERS)
    message(FATAL_ERROR "No se encontro onnxruntime_c_api.h en ${onnxruntime_SOURCE_DIR}")
endif()
list(GET ORT_HEADERS 0 ORT_HEADER_FIRST)
get_filename_component(ORT_INCLUDE_DIR "${ORT_HEADER_FIRST}" DIRECTORY)

file(GLOB_RECURSE ORT_DLLS "${onnxruntime_SOURCE_DIR}/*/onnxruntime.dll")
if(NOT ORT_DLLS)
    message(FATAL_ERROR "No se encontro onnxruntime.dll en ${onnxruntime_SOURCE_DIR}")
endif()
list(GET ORT_DLLS 0 ORT_DLL)
get_filename_component(ORT_LIB_DIR "${ORT_DLL}" DIRECTORY)

# --- Modelo de separacion de stems (HT-Demucs en formato ONNX, ~166 MB) ---
set(MODEL_URL "https://huggingface.co/StemSplitio/htdemucs-onnx/resolve/main/htdemucs_fp16weights.onnx")
set(MODEL_FILE "${CMAKE_BINARY_DIR}/models/htdemucs_fp16weights.onnx")
if(NOT EXISTS "${MODEL_FILE}")
    message(STATUS "Descargando el modelo de stems (puede tardar un poco)...")
    file(DOWNLOAD "${MODEL_URL}" "${MODEL_FILE}" STATUS MODEL_STATUS TLS_VERIFY ON)
    list(GET MODEL_STATUS 0 MODEL_CODE)
    if(NOT MODEL_CODE EQUAL 0)
        message(WARNING "No se pudo descargar el modelo de stems: ${MODEL_STATUS}")
        file(REMOVE "${MODEL_FILE}")
    endif()
endif()
if(EXISTS "${MODEL_FILE}")
    file(SIZE "${MODEL_FILE}" MODEL_SIZE)
    if(MODEL_SIZE LESS 50000000)
        message(WARNING "El modelo descargado parece incompleto (${MODEL_SIZE} bytes)")
        file(REMOVE "${MODEL_FILE}")
    endif()
endif()

juce_add_plugin(TongaSampler2
    COMPANY_NAME "Tonga"
    PLUGIN_MANUFACTURER_CODE Tong
    PLUGIN_CODE Tsm2
    FORMATS VST3
    PRODUCT_NAME "Tonga Sampler 2"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_add_binary_data(TongaAssets SOURCES Assets/TONGA_SAMPLER.png)

target_sources(TongaSampler2 PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/StemSeparator.cpp
    Source/StemSeparator.h)

target_include_directories(TongaSampler2 PRIVATE "${ORT_INCLUDE_DIR}")

target_compile_definitions(TongaSampler2 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(TongaSampler2 PRIVATE
    TongaAssets
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

juce_generate_juce_header(TongaSampler2)

# --- Dejar onnxruntime.dll y el modelo dentro de la carpeta del .vst3 ---
if(TARGET TongaSampler2_VST3)
    add_custom_command(TARGET TongaSampler2_VST3 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DLL}" "$<TARGET_FILE_DIR:TongaSampler2_VST3>/"
        COMMENT "Copiando onnxruntime.dll")

    if(EXISTS "${ORT_LIB_DIR}/onnxruntime_providers_shared.dll")
        add_custom_command(TARGET TongaSampler2_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_LIB_DIR}/onnxruntime_providers_shared.dll" "$<TARGET_FILE_DIR:TongaSampler2_VST3>/")
    endif()

    if(EXISTS "${MODEL_FILE}")
        add_custom_command(TARGET TongaSampler2_VST3 POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TongaSampler2_VST3>/../Resources"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MODEL_FILE}" "$<TARGET_FILE_DIR:TongaSampler2_VST3>/../Resources/"
            COMMENT "Copiando el modelo de stems")
    endif()
endif()
